#pragma once

// Execution handler -- manages the order lifecycle after routing.
//
// Tracks parent/child order relationships, processes execution reports
// from venues, propagates fills upward, and triggers rerouting when
// child orders are rejected or canceled with remaining quantity.

#include "core/types.h"
#include "core/order.h"
#include "core/spsc_queue.h"
#include <unordered_map>
#include <functional>
#include <vector>
#include <mutex>

namespace sor::execution
{

    class ExecutionHandler
    {
    public:
        using FillCallback = std::function<void(const Order &, const ExecutionReport &)>;
        using CompletionCallback = std::function<void(const Order &)>;
        using RerouteCallback = std::function<void(Order &)>;

        ExecutionHandler();

        // Register a new parent order being tracked.
        void track_order(const Order &parent);

        // Register child orders created from routing slices.
        void track_child_order(OrderId parent_id, const Order &child);

        // Process an execution report from a venue.
        void on_execution_report(const ExecutionReport &report);

        // Get order by ID (const).
        const Order *get_order(OrderId id) const;

        // Get order by ID (mutable).
        Order *get_mutable_order(OrderId id);

        // Get children of a parent order.
        std::vector<OrderId> get_children(OrderId parent_id) const;

        // Get parent of a child order.
        OrderId get_parent(OrderId child_id) const;

        // Is the parent order fully filled?
        bool is_complete(OrderId parent_id) const;

        // Callbacks.
        void set_fill_callback(FillCallback cb) { fill_callback_ = std::move(cb); }
        void set_completion_callback(CompletionCallback cb) { completion_callback_ = std::move(cb); }
        void set_reroute_callback(RerouteCallback cb) { reroute_callback_ = std::move(cb); }

        // Aggregated statistics.
        struct Stats
        {
            uint64_t total_fills{0};
            uint64_t total_partial_fills{0};
            uint64_t total_rejects{0};
            uint64_t total_cancels{0};
            uint64_t reroutes{0};
        };
        Stats get_stats() const { return stats_; }

    private:
        // Propagate child fill information to the parent order.
        void update_parent_from_child(Order &parent, const Order &child,
                                      const ExecutionReport &report);

        // Check whether a parent order needs rerouting after a child terminal event.
        void check_and_reroute(Order &parent);

        std::unordered_map<OrderId, Order> orders_; // all orders (parent + child)
        std::unordered_map<OrderId, std::vector<OrderId>> parent_to_children_;
        std::unordered_map<OrderId, OrderId> child_to_parent_;

        FillCallback fill_callback_;
        CompletionCallback completion_callback_;
        RerouteCallback reroute_callback_;

        mutable std::mutex mutex_;
        Stats stats_;
    };

} // namespace sor::execution
