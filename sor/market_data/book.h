#pragma once

// L2 order book per venue.
// Maintains sorted price levels for bids (descending) and asks (ascending).
// Designed for cache-friendly traversal with fixed-size arrays.

#include "core/types.h"
#include <array>
#include <algorithm>

namespace sor::market_data
{

    static constexpr size_t MAX_DEPTH = 20;

    struct PriceLevel
    {
        Price price{INVALID_PRICE};
        Quantity quantity{0};
        int32_t order_count{0};

        [[nodiscard]] bool valid() const noexcept
        {
            return price != INVALID_PRICE && quantity > 0;
        }
    };

    struct BookSide
    {
        std::array<PriceLevel, MAX_DEPTH> levels{};
        size_t depth{0}; // number of valid levels

        // Top of book
        [[nodiscard]] const PriceLevel &best() const noexcept { return levels[0]; }
        [[nodiscard]] bool empty() const noexcept { return depth == 0; }

        // Update a price level. If quantity==0, remove it.
        void update(Price price, Quantity qty, int32_t order_count);

        // Insert maintaining price priority (bids: descending, asks: ascending)
        void insert_sorted(const PriceLevel &level, bool is_bid);

        // Remove a price level
        void remove(Price price);

        // Clear all levels
        void clear() noexcept;
    };

    struct OrderBook
    {
        Symbol symbol;
        VenueId venue_id{0};
        BookSide bids;
        BookSide asks;
        Timestamp last_update{};
        uint64_t sequence{0};

        [[nodiscard]] Price best_bid() const noexcept
        {
            return bids.empty() ? INVALID_PRICE : bids.best().price;
        }

        [[nodiscard]] Price best_ask() const noexcept
        {
            return asks.empty() ? INVALID_PRICE : asks.best().price;
        }

        [[nodiscard]] Price mid_price() const noexcept;
        [[nodiscard]] Price spread() const noexcept;

        // Get quantity available at or better than price
        [[nodiscard]] Quantity quantity_at_or_better(Side side, Price price) const noexcept;

        // Is book crossed? (error condition: best bid >= best ask)
        [[nodiscard]] bool is_crossed() const noexcept;

        void clear() noexcept;
    };

} // namespace sor::market_data
