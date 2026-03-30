#pragma once

// Fill manager -- records, indexes, and aggregates fill events.
//
// Every fill from any venue is recorded as a FillRecord.  The manager
// maintains indices by order ID and by symbol so that queries and
// aggregations (VWAP, total fees, etc.) are O(n) in the number of
// fills for the queried key rather than across all fills.

#include "core/types.h"
#include "core/order.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace sor::execution
{

    struct FillRecord
    {
        OrderId order_id{INVALID_ORDER_ID};
        OrderId exec_id{INVALID_ORDER_ID};
        Symbol symbol;
        Side side{Side::Buy};
        Price price{0};
        Quantity quantity{0};
        VenueId venue_id{0};
        double fee{0.0};
        Timestamp timestamp;
    };

    class FillManager
    {
    public:
        // Record a fill event.
        void record_fill(const FillRecord &fill);

        // Query fills.
        std::vector<FillRecord> get_fills_for_order(OrderId order_id) const;
        std::vector<FillRecord> get_fills_for_symbol(const Symbol &symbol) const;
        std::vector<FillRecord> get_all_fills() const;

        // Aggregated stats for a single order.
        Quantity total_filled_quantity(OrderId order_id) const;
        Price average_fill_price(OrderId order_id) const;
        double total_fees(OrderId order_id) const;

        // VWAP of all fills for a symbol.
        Price vwap(const Symbol &symbol) const;

        // Clear all recorded fills.
        void clear();

    private:
        std::vector<FillRecord> all_fills_;
        std::unordered_map<OrderId, std::vector<size_t>> order_fills_; // order_id -> indices
        std::unordered_map<Symbol, std::vector<size_t>> symbol_fills_; // symbol   -> indices
        mutable std::mutex mutex_;
    };

} // namespace sor::execution
