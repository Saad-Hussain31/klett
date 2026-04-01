#pragma once

// NBBO aggregation across multiple venues.
// Maintains per-venue books and computes the National Best Bid/Offer
// as well as aggregated depth views for routing decisions.

#include "core/types.h"
#include "market_data/book.h"
#include <unordered_map>
#include <vector>
#include <functional>
#include <chrono>
#include <shared_mutex>

namespace sor::market_data
{

    struct NBBO
    {
        Price best_bid{INVALID_PRICE};
        Quantity best_bid_qty{0};
        VenueId best_bid_venue{0};
        Price best_ask{INVALID_PRICE};
        Quantity best_ask_qty{0};
        VenueId best_ask_venue{0};
        Timestamp timestamp{};

        [[nodiscard]] Price spread() const noexcept;
        [[nodiscard]] Price mid_price() const noexcept;
        [[nodiscard]] bool valid() const noexcept;
    };

    // Aggregated view across all venues for a symbol.
    struct AggregatedBook
    {
        Symbol symbol;
        NBBO nbbo;

        // Merged depth: sorted price levels with venue attribution.
        struct VenueQuantity
        {
            VenueId venue_id{0};
            Quantity quantity{0};
        };

        struct AggregatedLevel
        {
            Price price{INVALID_PRICE};
            Quantity total_quantity{0};
            std::array<VenueQuantity, 16> venue_breakdown{};
            size_t venue_count{0};

            void add_venue(VenueId venue_id, Quantity qty);
        };

        std::array<AggregatedLevel, MAX_DEPTH> bids{};
        std::array<AggregatedLevel, MAX_DEPTH> asks{};
        size_t bid_depth{0};
        size_t ask_depth{0};
    };

    class MarketDataAggregator
    {
    public:
        using NBBOCallback = std::function<void(const Symbol &, const NBBO &)>;

        // Register a venue's book.
        void register_venue(VenueId venue_id);

        // Update a venue's book for a symbol.
        void on_book_update(VenueId venue_id, const Symbol &symbol, const OrderBook &book);

        // Get NBBO for a symbol.
        [[nodiscard]] NBBO get_nbbo(const Symbol &symbol) const;

        // Get full aggregated book.
        [[nodiscard]] AggregatedBook get_aggregated_book(const Symbol &symbol) const;

        // Get specific venue's book.
        [[nodiscard]] const OrderBook *get_venue_book(VenueId venue_id, const Symbol &symbol) const;

        // Staleness detection.
        [[nodiscard]] bool is_stale(const Symbol &symbol, std::chrono::microseconds max_age) const;

        // Register NBBO update callback.
        void set_nbbo_callback(NBBOCallback cb);

    private:
        void recalculate_nbbo(const Symbol &symbol);
        void build_aggregated_book(const Symbol &symbol, AggregatedBook &out) const;
        // Internal lookup without locking (caller must hold mutex_)
        const OrderBook *get_venue_book_unlocked(VenueId venue_id, const Symbol &symbol) const;

        // venue_id -> (symbol -> OrderBook)
        std::unordered_map<VenueId, std::unordered_map<Symbol, OrderBook>> venue_books_;
        std::unordered_map<Symbol, NBBO> nbbo_cache_;
        std::vector<VenueId> venues_;
        NBBOCallback nbbo_callback_;
        mutable std::shared_mutex mutex_;
    };

} // namespace sor::market_data
