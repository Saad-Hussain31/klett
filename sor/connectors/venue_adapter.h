#pragma once

// Abstract venue adapter interface.
// All concrete venue connections (simulated exchanges, FIX gateways, etc.)
// implement this interface so the routing engine can treat them uniformly.

#include "core/types.h"
#include "core/order.h"
#include <functional>
#include <string>
#include <atomic>
#include <chrono>

namespace sor::connectors
{

    class VenueAdapter
    {
    public:
        using ExecutionCallback = std::function<void(const ExecutionReport &)>;

        virtual ~VenueAdapter() = default;

        // -- Connection lifecycle ---------------------------------------------------

        /// Establish the connection to the venue.  Returns true on success.
        virtual bool connect() = 0;

        /// Gracefully disconnect from the venue.
        virtual void disconnect() = 0;

        /// Thread-safe check for current connection state.
        [[nodiscard]] virtual bool is_connected() const = 0;

        // -- Order operations -------------------------------------------------------

        /// Submit an order to the venue.  Returns true if the order was accepted
        /// for transmission (does NOT imply venue acknowledgement).
        virtual bool send_order(const Order &order) = 0;

        /// Request cancellation of an outstanding order.
        virtual bool cancel_order(const CancelRequest &request) = 0;

        // -- Venue metadata ---------------------------------------------------------

        [[nodiscard]] virtual VenueId venue_id() const = 0;
        [[nodiscard]] virtual const char *venue_name() const = 0;
        [[nodiscard]] virtual VenueStatus status() const = 0;

        // -- Latency ----------------------------------------------------------------

        /// Rolling average round-trip latency for this venue.
        [[nodiscard]] virtual std::chrono::microseconds avg_latency() const = 0;

        // -- Callback registration --------------------------------------------------

        void set_execution_callback(ExecutionCallback cb)
        {
            exec_callback_ = std::move(cb);
        }

    protected:
        /// Dispatch an execution report to the registered callback.
        void notify_execution(const ExecutionReport &report)
        {
            if (exec_callback_)
                exec_callback_(report);
        }

        ExecutionCallback exec_callback_;
    };

} // namespace sor::connectors
