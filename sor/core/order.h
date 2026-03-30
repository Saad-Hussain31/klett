#pragma once

// Order, ExecutionReport, and CancelRequest structures.
// All structs are cache-line aligned and avoid heap allocations.

#include "core/types.h"

namespace sor
{

    // ---------------------------------------------------------------------------
    // Order -- the primary order representation.
    //
    // Aligned to 64 bytes (cache line) to avoid false sharing when orders
    // are stored in contiguous arrays and accessed from multiple threads.
    // ---------------------------------------------------------------------------

    struct alignas(64) Order
    {
        OrderId id{INVALID_ORDER_ID};
        OrderId client_order_id{INVALID_ORDER_ID};
        OrderId parent_order_id{INVALID_ORDER_ID}; // non-zero for child orders

        Symbol symbol;
        Side side{Side::Buy};
        OrderType type{OrderType::Limit};
        TimeInForce tif{TimeInForce::GTC};

        Price price{INVALID_PRICE};
        Quantity quantity{0};
        Quantity filled_quantity{0};
        Quantity remaining_quantity{0};
        Price avg_fill_price{0};

        VenueId target_venue{0};
        RoutingStrategy strategy{RoutingStrategy::BestPrice};
        OrderState state{OrderState::New};

        ClientId client_id;

        Timestamp create_time;
        Timestamp last_update_time;

        uint32_t version{0}; // optimistic concurrency control

        // -- Derived accessors --------------------------------------------------

        /// Quantity remaining to be filled (quantity minus what has executed).
        [[nodiscard]] Quantity leaves_qty() const noexcept
        {
            return quantity - filled_quantity;
        }

        /// True when the order has reached a final state (no further transitions).
        [[nodiscard]] bool is_terminal() const noexcept;

        /// True when the order can still receive fills or be acted upon.
        [[nodiscard]] bool is_active() const noexcept;
    };

    // ---------------------------------------------------------------------------
    // ExecutionReport -- venue execution acknowledgement / fill notification.
    // ---------------------------------------------------------------------------

    struct alignas(64) ExecutionReport
    {
        OrderId order_id{INVALID_ORDER_ID};
        OrderId exec_id{INVALID_ORDER_ID};
        OrderState state{OrderState::New};

        Price last_price{0};         // price of last fill
        Quantity last_quantity{0};   // quantity of last fill
        Price avg_price{0};          // cumulative average fill price
        Quantity cum_quantity{0};    // cumulative filled quantity
        Quantity leaves_quantity{0}; // remaining quantity

        VenueId venue_id{0};
        Timestamp timestamp;

        FixedString<64> text; // reject reason, informational text
    };

    // ---------------------------------------------------------------------------
    // CancelRequest -- request to cancel an outstanding order.
    // ---------------------------------------------------------------------------

    struct alignas(64) CancelRequest
    {
        OrderId order_id{INVALID_ORDER_ID};
        OrderId client_order_id{INVALID_ORDER_ID};
        Symbol symbol;
        Side side{Side::Buy};
        Timestamp timestamp;
    };

} // namespace sor
