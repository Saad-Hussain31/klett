// simulated_exchange.cpp -- Full matching engine implementation for the
// simulated exchange.  Supports Market, Limit, IOC, and FOK order types
// with configurable partial fill, reject, and latency behaviour.

#include "connectors/simulated_exchange.h"
#include "infra/logging.h"

#include <algorithm>
#include <cassert>

namespace sor::connectors
{

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    SimulatedExchange::SimulatedExchange(Config config)
        : config_(std::move(config)), rng_(std::random_device{}())
    {
    }

    // ---------------------------------------------------------------------------
    // VenueAdapter -- connection lifecycle
    // ---------------------------------------------------------------------------

    bool SimulatedExchange::connect()
    {
        bool expected = false;
        if (!connected_.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel))
        {
            return false; // already connected
        }
        return true;
    }

    void SimulatedExchange::disconnect()
    {
        connected_.store(false, std::memory_order_release);
    }

    bool SimulatedExchange::is_connected() const
    {
        return connected_.load(std::memory_order_acquire);
    }

    // ---------------------------------------------------------------------------
    // VenueAdapter -- order operations
    // ---------------------------------------------------------------------------

    bool SimulatedExchange::send_order(const Order &order)
    {
        if (!is_connected())
        {
            SOR_LOG_WARN("[SimExchange:{}] Order {} rejected: not connected", config_.name, order.id);
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        InternalOrder internal{};
        internal.order = order;
        internal.received_time = std::chrono::steady_clock::now();
        internal.pending_cancel = false;

        auto [it, inserted] = active_orders_.emplace(order.id, internal);
        if (!inserted)
            return false; // duplicate OrderId

        pending_queue_.push(order.id);
        ++stats_.orders_received;

        // Immediately generate an Accepted acknowledgement.
        ExecutionReport ack{};
        ack.order_id = order.id;
        ack.exec_id = next_exec_id_++;
        ack.state = OrderState::Accepted;
        ack.last_price = 0;
        ack.last_quantity = 0;
        ack.avg_price = 0;
        ack.cum_quantity = 0;
        ack.leaves_quantity = order.quantity;
        ack.venue_id = config_.venue_id;
        ack.timestamp = std::chrono::steady_clock::now();
        notify_execution(ack);

        return true;
    }

    bool SimulatedExchange::cancel_order(const CancelRequest &request)
    {
        if (!is_connected())
            return false;

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = active_orders_.find(request.order_id);
        if (it == active_orders_.end())
            return false; // unknown order

        it->second.pending_cancel = true;
        return true;
    }

    // ---------------------------------------------------------------------------
    // VenueAdapter -- metadata & latency
    // ---------------------------------------------------------------------------

    VenueId SimulatedExchange::venue_id() const
    {
        return config_.venue_id;
    }

    const char *SimulatedExchange::venue_name() const
    {
        return config_.name.c_str();
    }

    VenueStatus SimulatedExchange::status() const
    {
        return is_connected() ? VenueStatus::Connected : VenueStatus::Disconnected;
    }

    std::chrono::microseconds SimulatedExchange::avg_latency() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latency_samples_ == 0)
            return config_.latency;
        return std::chrono::microseconds(
            total_latency_.count() / static_cast<int64_t>(latency_samples_));
    }

    // ---------------------------------------------------------------------------
    // Market price & statistics
    // ---------------------------------------------------------------------------

    void SimulatedExchange::set_market_price(Price bid, Price ask)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        market_bid_ = bid;
        market_ask_ = ask;
    }

    SimulatedExchange::Stats SimulatedExchange::get_stats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return stats_;
    }

    // ---------------------------------------------------------------------------
    // Matching engine -- main loop
    // ---------------------------------------------------------------------------

    void SimulatedExchange::process_matching()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        const size_t count = pending_queue_.size();
        for (size_t i = 0; i < count; ++i)
        {
            const OrderId id = pending_queue_.front();
            pending_queue_.pop();

            auto it = active_orders_.find(id);
            if (it == active_orders_.end())
                continue; // already removed

            auto &internal = it->second;

            // Handle pending cancellation before attempting a match.
            if (internal.pending_cancel)
            {
                generate_cancel_ack(internal.order);
                active_orders_.erase(it);
                continue;
            }

            const bool keep_active = try_match(internal);
            if (!keep_active)
            {
                active_orders_.erase(it);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Matching engine -- single-order matching
    // ---------------------------------------------------------------------------

    bool SimulatedExchange::try_match(InternalOrder &internal)
    {
        Order &order = internal.order;
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        // ---- Random rejection (simulates exchange-side rejects) ----------------
        if (unit(rng_) < config_.reject_probability)
        {
            generate_reject(order, "Exchange rejected order");
            return false;
        }

        // ---- Determine executability and fill price ----------------------------
        Price fill_price = INVALID_PRICE;
        bool executable = false;

        const bool is_market = (order.type == OrderType::Market);

        if (is_market)
        {
            if (order.side == Side::Buy && market_ask_ != INVALID_PRICE)
            {
                fill_price = market_ask_;
                executable = true;
            }
            else if (order.side == Side::Sell && market_bid_ != INVALID_PRICE)
            {
                fill_price = market_bid_;
                executable = true;
            }
        }
        else
        {
            // Limit / IOC / FOK -- check price level against market.
            if (order.side == Side::Buy && market_ask_ != INVALID_PRICE && order.price >= market_ask_)
            {
                fill_price = market_ask_;
                executable = true;
            }
            else if (order.side == Side::Sell && market_bid_ != INVALID_PRICE && order.price <= market_bid_)
            {
                fill_price = market_bid_;
                executable = true;
            }
        }

        // Classify time-in-force semantics from either OrderType or TimeInForce.
        const bool is_ioc = (order.type == OrderType::IOC || order.tif == TimeInForce::IOC);
        const bool is_fok = (order.type == OrderType::FOK || order.tif == TimeInForce::FOK);

        // ---- Not executable ----------------------------------------------------
        if (!executable)
        {
            if (is_fok)
            {
                generate_reject(order, "FOK order not fully executable");
                return false;
            }
            if (is_ioc)
            {
                // Nothing available to fill -- cancel immediately.
                generate_cancel_ack(order);
                return false;
            }
            // GTC / Day limit: leave in the book for future matching.
            pending_queue_.push(order.id);
            return true;
        }

        // ---- Fill-probability gate (simulates thin liquidity) ------------------
        if (unit(rng_) > config_.fill_probability)
        {
            if (is_fok)
            {
                generate_reject(order, "FOK fill probability miss");
                return false;
            }
            if (is_ioc)
            {
                generate_cancel_ack(order);
                return false;
            }
            pending_queue_.push(order.id);
            return true;
        }

        // ---- Compute remaining quantity ----------------------------------------
        const Quantity remaining = order.quantity - order.filled_quantity;
        assert(remaining > 0);

        // ---- FOK: must fill the entire remaining quantity ----------------------
        if (is_fok)
        {
            generate_fill(internal, fill_price, remaining);
            return false;
        }

        // ---- Partial fill logic ------------------------------------------------
        if (unit(rng_) < config_.partial_fill_probability && remaining > 1)
        {
            std::uniform_real_distribution<double> pct(0.1, 0.9);
            Quantity fill_qty = static_cast<Quantity>(
                static_cast<double>(remaining) * pct(rng_));
            // Clamp: at least 1, at most remaining-1 to guarantee a partial.
            fill_qty = std::clamp(fill_qty, Quantity{1}, remaining - 1);

            generate_fill(internal, fill_price, fill_qty);

            if (is_ioc)
            {
                // IOC: cancel whatever is left after the partial.
                generate_cancel_ack(order);
                return false;
            }
            // Re-queue for full fill on the next process_matching() cycle.
            pending_queue_.push(order.id);
            return true;
        }

        // ---- Full fill ---------------------------------------------------------
        generate_fill(internal, fill_price, remaining);
        return false;
    }

    // ---------------------------------------------------------------------------
    // Execution report generators
    // ---------------------------------------------------------------------------

    void SimulatedExchange::generate_fill(
        InternalOrder &internal, Price fill_price, Quantity fill_qty)
    {
        Order &order = internal.order;

        const Quantity prev_filled = order.filled_quantity;
        order.filled_quantity += fill_qty;
        const Quantity new_remaining = order.quantity - order.filled_quantity;

        // Weighted average fill price.
        // Use double intermediate to avoid int64 overflow on the product
        // (Price * Quantity can exceed 2^63 for large values).
        if (order.filled_quantity > 0)
        {
            const double old_component =
                static_cast<double>(order.avg_fill_price) * static_cast<double>(prev_filled);
            const double new_component =
                static_cast<double>(fill_price) * static_cast<double>(fill_qty);
            order.avg_fill_price = static_cast<Price>(
                (old_component + new_component) / static_cast<double>(order.filled_quantity));
        }

        const bool is_full = (new_remaining == 0);

        ExecutionReport report{};
        report.order_id = order.id;
        report.exec_id = next_exec_id_++;
        report.state = is_full ? OrderState::Filled
                               : OrderState::PartiallyFilled;
        report.last_price = fill_price;
        report.last_quantity = fill_qty;
        report.avg_price = order.avg_fill_price;
        report.cum_quantity = order.filled_quantity;
        report.leaves_quantity = new_remaining;
        report.venue_id = config_.venue_id;
        report.timestamp = std::chrono::steady_clock::now();

        if (is_full)
        {
            ++stats_.orders_filled;
        }
        else
        {
            ++stats_.orders_partially_filled;
        }

        // Track latency from order receipt to fill generation.
        const auto now = std::chrono::steady_clock::now();
        const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
            now - internal.received_time);
        total_latency_ += latency;
        ++latency_samples_;

        notify_execution(report);
    }

    void SimulatedExchange::generate_reject(const Order &order, const char *reason)
    {
        SOR_LOG_WARN("[SimExchange:{}] Rejected order {}: {}", config_.name, order.id, reason);
        ExecutionReport report{};
        report.order_id = order.id;
        report.exec_id = next_exec_id_++;
        report.state = OrderState::Rejected;
        report.last_price = 0;
        report.last_quantity = 0;
        report.avg_price = order.avg_fill_price;
        report.cum_quantity = order.filled_quantity;
        report.leaves_quantity = 0;
        report.venue_id = config_.venue_id;
        report.timestamp = std::chrono::steady_clock::now();
        report.text = reason;

        ++stats_.orders_rejected;
        notify_execution(report);
    }

    void SimulatedExchange::generate_cancel_ack(const Order &order)
    {
        ExecutionReport report{};
        report.order_id = order.id;
        report.exec_id = next_exec_id_++;
        report.state = OrderState::Canceled;
        report.last_price = 0;
        report.last_quantity = 0;
        report.avg_price = order.avg_fill_price;
        report.cum_quantity = order.filled_quantity;
        report.leaves_quantity = 0;
        report.venue_id = config_.venue_id;
        report.timestamp = std::chrono::steady_clock::now();

        ++stats_.orders_canceled;
        notify_execution(report);
    }

} // namespace sor::connectors
