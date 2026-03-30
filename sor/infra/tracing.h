#pragma once

// Order-level tracing for the Smart Order Router.
//
// Captures a per-order timeline of events (submit -> risk check -> route ->
// venue send -> fill) with microsecond-precision timestamps. Useful for
// post-trade analysis and latency debugging.

#include "core/types.h"

#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <cstddef>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // A single timestamped event in an order's lifecycle.
    // ---------------------------------------------------------------------------

    struct TraceEvent
    {
        OrderId order_id;
        std::string stage;  // e.g., "submit", "risk_check", "route", "venue_send", "fill"
        std::string detail; // free-form diagnostic info
        Timestamp timestamp;
        std::chrono::microseconds latency_from_start{0};
    };

    // ---------------------------------------------------------------------------
    // Tracer singleton -- thread-safe, retained in memory for post-mortem.
    // ---------------------------------------------------------------------------

    class Tracer
    {
    public:
        static Tracer &instance();

        /// Begin tracing an order.  Records the start timestamp.
        void begin_trace(OrderId order_id);

        /// Record an intermediate trace event.
        void trace(OrderId order_id,
                   const std::string &stage,
                   const std::string &detail = "");

        /// Mark the trace as complete (the order has reached a terminal state).
        void end_trace(OrderId order_id);

        /// Retrieve the full event list for an order.
        [[nodiscard]] std::vector<TraceEvent> get_trace(OrderId order_id) const;

        /// Dump an order's trace to a human-readable string (for logging).
        [[nodiscard]] std::string dump_trace(OrderId order_id) const;

        /// Garbage-collect old completed traces, keeping at most @p max_traces.
        void gc(std::size_t max_traces = 10000);

        /// True if the order is currently being traced and has not ended.
        [[nodiscard]] bool is_tracing(OrderId order_id) const;

    private:
        Tracer() = default;
        ~Tracer() = default;
        Tracer(const Tracer &) = delete;
        Tracer &operator=(const Tracer &) = delete;

        struct OrderTrace
        {
            Timestamp start;
            std::vector<TraceEvent> events;
            bool active{true};
        };

        std::unordered_map<OrderId, OrderTrace> traces_;
        mutable std::mutex mutex_;
    };

} // namespace sor::infra
