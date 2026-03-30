#include "gateway/gateway.h"
#include "state/order_state_machine.h"
#include <chrono>

namespace sor::gateway
{

    // ---------------------------------------------------------------------------
    // Construction / destruction
    // ---------------------------------------------------------------------------

    Gateway::Gateway(Config config)
        : config_(std::move(config)), routing_engine_(std::make_unique<routing::RoutingEngine>(md_aggregator_, risk_manager_))
    {
    }

    Gateway::~Gateway()
    {
        stop();
    }

    // ---------------------------------------------------------------------------
    // Lifecycle
    // ---------------------------------------------------------------------------

    bool Gateway::initialize()
    {
        // Wire execution handler callbacks.

        // On fill: record in fill manager and update risk manager.
        exec_handler_.set_fill_callback(
            [this](const Order &order, const ExecutionReport &report)
            {
                execution::FillRecord record;
                record.order_id = report.order_id;
                record.exec_id = report.exec_id;
                record.symbol = order.symbol;
                record.side = order.side;
                record.price = report.last_price;
                record.quantity = report.last_quantity;
                record.venue_id = report.venue_id;
                record.timestamp = report.timestamp;
                fill_manager_.record_fill(record);

                risk_manager_.on_fill(order.symbol, order.side,
                                      report.last_quantity, report.last_price);
            });

        // On parent order completion.
        exec_handler_.set_completion_callback(
            [this](const Order &order)
            {
                ++stats_.orders_completed;
            });

        // On reroute: re-route the parent order for its remaining quantity.
        exec_handler_.set_reroute_callback(
            [this](Order &parent)
            {
                // Adjust the parent's quantity to the remaining amount and re-route.
                Order reroute_order = parent;
                reroute_order.quantity = parent.remaining_quantity;
                reroute_order.filled_quantity = 0;
                reroute_order.remaining_quantity = reroute_order.quantity;
                reroute_order.state = OrderState::New;

                process_single_order(reroute_order);
            });

        // Connect all registered venue adapters.
        for (auto &[vid, adapter] : venues_)
        {
            if (!adapter->connect())
            {
                return false;
            }
        }

        return true;
    }

    void Gateway::start()
    {
        if (running_.exchange(true))
        {
            return; // Already running.
        }

        order_thread_ = std::thread([this]
                                    { order_processing_loop(); });
        exec_thread_ = std::thread([this]
                                   { execution_processing_loop(); });
    }

    void Gateway::stop()
    {
        if (!running_.exchange(false))
        {
            return; // Already stopped.
        }

        if (order_thread_.joinable())
        {
            order_thread_.join();
        }
        if (exec_thread_.joinable())
        {
            exec_thread_.join();
        }

        // Disconnect all venues.
        for (auto &[vid, adapter] : venues_)
        {
            adapter->disconnect();
        }
    }

    bool Gateway::is_running() const
    {
        return running_.load(std::memory_order_acquire);
    }

    // ---------------------------------------------------------------------------
    // Order submission (thread-safe, non-blocking)
    // ---------------------------------------------------------------------------

    bool Gateway::submit_order(Order order)
    {
        if (!running_.load(std::memory_order_acquire)) [[unlikely]]
        {
            return false;
        }

        // Assign a unique order ID if one has not been set.
        if (order.id == INVALID_ORDER_ID)
        {
            order.id = generate_order_id();
        }
        order.create_time = std::chrono::steady_clock::now();

        if (!order_queue_.try_push(std::move(order))) [[unlikely]]
        {
            return false; // Queue full.
        }

        ++stats_.orders_submitted;
        return true;
    }

    bool Gateway::cancel_order(CancelRequest request)
    {
        if (!running_.load(std::memory_order_acquire)) [[unlikely]]
        {
            return false;
        }

        request.timestamp = std::chrono::steady_clock::now();
        return cancel_queue_.try_push(std::move(request));
    }

    // ---------------------------------------------------------------------------
    // Venue management
    // ---------------------------------------------------------------------------

    void Gateway::add_venue(std::unique_ptr<connectors::VenueAdapter> adapter)
    {
        const VenueId vid = adapter->venue_id();

        // Wire venue execution reports into our report queue.
        adapter->set_execution_callback(
            [this](const ExecutionReport &report)
            {
                on_execution_report(report);
            });

        // Register the venue with the market data aggregator.
        md_aggregator_.register_venue(vid);

        venues_.emplace(vid, std::move(adapter));
    }

    // ---------------------------------------------------------------------------
    // Order query
    // ---------------------------------------------------------------------------

    const Order *Gateway::get_order(OrderId id) const
    {
        return exec_handler_.get_order(id);
    }

    Gateway::Stats Gateway::get_stats() const
    {
        return stats_;
    }

    // ---------------------------------------------------------------------------
    // Processing loops
    // ---------------------------------------------------------------------------

    void Gateway::order_processing_loop()
    {
        Order order;
        CancelRequest cancel;

        while (running_.load(std::memory_order_acquire))
        {
            bool did_work = false;

            // Drain order queue.
            while (order_queue_.try_pop(order))
            {
                process_single_order(order);
                did_work = true;
            }

            // Drain cancel queue.
            while (cancel_queue_.try_pop(cancel))
            {
                // Forward the cancel to the appropriate venue.
                const Order *existing = exec_handler_.get_order(cancel.order_id);
                if (existing)
                {
                    // Apply PendingCancel state to all active children.
                    auto children = exec_handler_.get_children(cancel.order_id);
                    for (OrderId child_id : children)
                    {
                        const Order *child = exec_handler_.get_order(child_id);
                        if (child && child->is_active())
                        {
                            CancelRequest child_cancel;
                            child_cancel.order_id = child_id;
                            child_cancel.client_order_id = cancel.client_order_id;
                            child_cancel.symbol = cancel.symbol;
                            child_cancel.side = cancel.side;
                            child_cancel.timestamp = cancel.timestamp;

                            auto vit = venues_.find(child->target_venue);
                            if (vit != venues_.end())
                            {
                                vit->second->cancel_order(child_cancel);
                            }
                        }
                    }
                }
                did_work = true;
            }

            // Yield when idle to avoid busy-spinning.
            if (!did_work)
            {
                std::this_thread::yield();
            }
        }
    }

    void Gateway::execution_processing_loop()
    {
        ExecutionReport report;

        while (running_.load(std::memory_order_acquire))
        {
            bool did_work = false;

            while (report_queue_.try_pop(report))
            {
                exec_handler_.on_execution_report(report);
                did_work = true;
            }

            if (!did_work)
            {
                std::this_thread::yield();
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Core order processing pipeline
    // ---------------------------------------------------------------------------

    void Gateway::process_single_order(Order &order)
    {
        // 1. Transition: New -> PendingNew.
        state::OrderStateMachine::apply(order, state::OrderEvent::Submit);

        // 2. Risk check.
        auto risk_result = risk_manager_.check_order(order);
        if (risk_result != risk::RiskCheckResult::Passed)
        {
            order.state = OrderState::Rejected;
            ++stats_.orders_rejected;
            return;
        }

        // 3. Track the parent order.
        exec_handler_.track_order(order);

        // 4. Route: produce slices.
        routing::RoutingDecision decision = routing_engine_->route_order(order);
        if (!decision.valid())
        {
            order.state = OrderState::Rejected;
            ++stats_.orders_rejected;
            return;
        }

        ++stats_.orders_routed;

        // 5. For each slice, create a child order, track it, and send to the venue.
        for (const auto &slice : decision.slices)
        {
            Order child{};
            child.id = generate_order_id();
            child.client_order_id = order.client_order_id;
            child.parent_order_id = order.id;
            child.symbol = order.symbol;
            child.side = order.side;
            child.type = slice.type;
            child.tif = slice.tif;
            child.price = slice.price;
            child.quantity = slice.quantity;
            child.remaining_quantity = slice.quantity;
            child.target_venue = slice.venue_id;
            child.strategy = order.strategy;
            child.client_id = order.client_id;
            child.state = OrderState::New;
            child.create_time = std::chrono::steady_clock::now();

            // Transition: New -> PendingNew.
            state::OrderStateMachine::apply(child, state::OrderEvent::Submit);

            // Track the parent-child relationship.
            exec_handler_.track_child_order(order.id, child);

            // Notify the risk manager.
            risk_manager_.on_order_accepted(child);

            // Dispatch to venue.
            auto vit = venues_.find(slice.venue_id);
            if (vit != venues_.end())
            {
                vit->second->send_order(child);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Execution report ingress (called from venue adapter callbacks)
    // ---------------------------------------------------------------------------

    void Gateway::on_execution_report(const ExecutionReport &report)
    {
        // Push into the SPSC report queue.  The execution processing loop
        // drains this queue on its dedicated thread.
        report_queue_.try_push(report);
    }

    // ---------------------------------------------------------------------------
    // ID generation
    // ---------------------------------------------------------------------------

    OrderId Gateway::generate_order_id()
    {
        return next_order_id_.fetch_add(1, std::memory_order_relaxed);
    }

} // namespace sor::gateway
