#include "execution/execution_handler.h"
#include "state/order_state_machine.h"
#include <chrono>

namespace sor::execution
{

    ExecutionHandler::ExecutionHandler() = default;

    void ExecutionHandler::track_order(const Order &parent)
    {
        std::lock_guard lock(mutex_);
        orders_[parent.id] = parent;
        parent_to_children_.try_emplace(parent.id);
    }

    void ExecutionHandler::track_child_order(OrderId parent_id, const Order &child)
    {
        std::lock_guard lock(mutex_);
        orders_[child.id] = child;
        parent_to_children_[parent_id].push_back(child.id);
        child_to_parent_[child.id] = parent_id;
    }

    void ExecutionHandler::on_execution_report(const ExecutionReport &report)
    {
        // Deferred callbacks invoked after the lock is released to avoid
        // re-entrancy deadlocks (reroute -> send_order -> exec callback -> here).
        std::function<void()> deferred_reroute;
        std::function<void()> deferred_completion;
        std::function<void()> deferred_fill;

        {
            std::lock_guard lock(mutex_);

            auto it = orders_.find(report.order_id);
            if (it == orders_.end()) [[unlikely]]
                return;

            Order &order = it->second;

            using E = state::OrderEvent;
            E event{};

            switch (report.state)
            {
            case OrderState::Accepted:
                event = E::Acknowledge;
                break;
            case OrderState::PartiallyFilled:
                event = E::PartialFill;
                break;
            case OrderState::Filled:
                event = E::Fill;
                break;
            case OrderState::Canceled:
                if (order.state == OrderState::Accepted ||
                    order.state == OrderState::PartiallyFilled)
                    state::OrderStateMachine::apply(order, E::RequestCancel);
                event = E::CancelAck;
                break;
            case OrderState::Rejected:
                event = E::Reject;
                break;
            case OrderState::Expired:
                event = E::Expire;
                break;
            case OrderState::PendingCancel:
                event = E::RequestCancel;
                break;
            case OrderState::PendingReplace:
                event = E::RequestReplace;
                break;
            default:
                return;
            }

            state::OrderStateMachine::apply(order, event);

            if (report.last_quantity > 0)
            {
                const Quantity prev_filled = order.filled_quantity;
                order.filled_quantity = report.cum_quantity;
                order.remaining_quantity = order.quantity - order.filled_quantity;
                if (order.filled_quantity > 0)
                {
                    const int64_t prev_cost = order.avg_fill_price * prev_filled;
                    const int64_t new_cost = report.last_price * report.last_quantity;
                    order.avg_fill_price = (prev_cost + new_cost) / order.filled_quantity;
                }
            }

            auto parent_it = child_to_parent_.find(order.id);
            const bool is_child = parent_it != child_to_parent_.end();

            if (is_child)
            {
                auto pit = orders_.find(parent_it->second);
                if (pit != orders_.end())
                {
                    Order &parent = pit->second;

                    if (report.last_quantity > 0)
                        update_parent_from_child(parent, order, report);

                    // Child terminal (rejected/canceled) -> check reroute
                    if (order.is_terminal() && order.state != OrderState::Filled)
                    {
                        if (!parent.is_terminal())
                        {
                            const Quantity remaining = parent.quantity - parent.filled_quantity;
                            if (remaining > 0)
                            {
                                bool all_terminal = true;
                                auto cit_it = parent_to_children_.find(parent.id);
                                if (cit_it != parent_to_children_.end())
                                {
                                    for (OrderId cid : cit_it->second)
                                    {
                                        auto c = orders_.find(cid);
                                        if (c != orders_.end() && !c->second.is_terminal())
                                        {
                                            all_terminal = false;
                                            break;
                                        }
                                    }
                                }
                                if (all_terminal)
                                {
                                    parent.remaining_quantity = remaining;
                                    ++stats_.reroutes;
                                    if (reroute_callback_)
                                    {
                                        OrderId pid = parent.id;
                                        deferred_reroute = [this, pid]()
                                        {
                                            // Get mutable ref under lock
                                            Order *p = nullptr;
                                            {
                                                std::lock_guard lk(mutex_);
                                                auto found = orders_.find(pid);
                                                if (found != orders_.end())
                                                    p = &found->second;
                                            }
                                            if (p)
                                                reroute_callback_(*p);
                                        };
                                    }
                                }
                            }
                        }
                    }

                    // Parent fully filled
                    if (parent.filled_quantity >= parent.quantity)
                    {
                        parent.state = OrderState::Filled;
                        parent.remaining_quantity = 0;
                        parent.last_update_time = std::chrono::steady_clock::now();
                        ++parent.version;
                        if (completion_callback_)
                        {
                            Order copy = parent;
                            deferred_completion = [this, copy]()
                            { completion_callback_(copy); };
                        }
                    }
                }
            }
            else
            {
                if (order.state == OrderState::Filled && completion_callback_)
                {
                    Order copy = order;
                    deferred_completion = [this, copy]()
                    { completion_callback_(copy); };
                }
            }

            if (report.last_quantity > 0 && fill_callback_)
            {
                Order copy = order;
                ExecutionReport rpt = report;
                deferred_fill = [this, copy, rpt]()
                { fill_callback_(copy, rpt); };
            }

            switch (report.state)
            {
            case OrderState::Filled: ++stats_.total_fills; break;
            case OrderState::PartiallyFilled: ++stats_.total_partial_fills; break;
            case OrderState::Rejected: ++stats_.total_rejects; break;
            case OrderState::Canceled: ++stats_.total_cancels; break;
            default: break;
            }
        } // lock released

        if (deferred_fill) deferred_fill();
        if (deferred_reroute) deferred_reroute();
        if (deferred_completion) deferred_completion();
    }

    const Order *ExecutionHandler::get_order(OrderId id) const
    {
        std::lock_guard lock(mutex_);
        auto it = orders_.find(id);
        return it != orders_.end() ? &it->second : nullptr;
    }

    Order *ExecutionHandler::get_mutable_order(OrderId id)
    {
        std::lock_guard lock(mutex_);
        auto it = orders_.find(id);
        return it != orders_.end() ? &it->second : nullptr;
    }

    std::vector<OrderId> ExecutionHandler::get_children(OrderId parent_id) const
    {
        std::lock_guard lock(mutex_);
        auto it = parent_to_children_.find(parent_id);
        if (it != parent_to_children_.end())
            return it->second;
        return {};
    }

    OrderId ExecutionHandler::get_parent(OrderId child_id) const
    {
        std::lock_guard lock(mutex_);
        auto it = child_to_parent_.find(child_id);
        return it != child_to_parent_.end() ? it->second : INVALID_ORDER_ID;
    }

    bool ExecutionHandler::is_complete(OrderId parent_id) const
    {
        std::lock_guard lock(mutex_);
        auto it = orders_.find(parent_id);
        if (it == orders_.end())
            return false;
        return it->second.filled_quantity >= it->second.quantity;
    }

    void ExecutionHandler::update_parent_from_child(
        Order &parent, const Order & /*child*/, const ExecutionReport &report)
    {
        const Quantity prev_parent_filled = parent.filled_quantity;
        parent.filled_quantity += report.last_quantity;
        parent.remaining_quantity = parent.quantity - parent.filled_quantity;
        if (parent.filled_quantity > 0)
        {
            const int64_t prev_cost = parent.avg_fill_price * prev_parent_filled;
            const int64_t fill_cost = report.last_price * report.last_quantity;
            parent.avg_fill_price = (prev_cost + fill_cost) / parent.filled_quantity;
        }
        if (parent.state == OrderState::Accepted ||
            parent.state == OrderState::PendingNew ||
            parent.state == OrderState::New)
        {
            parent.state = OrderState::PartiallyFilled;
        }
        parent.last_update_time = std::chrono::steady_clock::now();
        ++parent.version;
    }

    void ExecutionHandler::check_and_reroute(Order &parent)
    {
        // No longer called directly — inlined into on_execution_report
        // for deferred-callback support. Kept for API compatibility.
        if (parent.is_terminal()) return;
        const Quantity remaining = parent.quantity - parent.filled_quantity;
        if (remaining <= 0) return;
        auto children_it = parent_to_children_.find(parent.id);
        if (children_it == parent_to_children_.end()) return;
        bool all_terminal = true;
        for (OrderId cid : children_it->second)
        {
            auto cit = orders_.find(cid);
            if (cit != orders_.end() && !cit->second.is_terminal())
            { all_terminal = false; break; }
        }
        if (all_terminal && remaining > 0)
        {
            parent.remaining_quantity = remaining;
            ++stats_.reroutes;
            if (reroute_callback_)
                reroute_callback_(parent);
        }
    }

} // namespace sor::execution
