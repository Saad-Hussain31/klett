#pragma once

// Alpaca V2 WebSocket message normalizer.
// Parses JSON quote/trade messages from the Alpaca streaming API
// and converts them into internal OrderBook updates.

#include "core/types.h"
#include "market_data/book.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace sor::market_data::alpaca
{

    class AlpacaNormalizer
    {
    public:
        using BookCallback = std::function<void(VenueId, const Symbol &, const OrderBook &)>;

        explicit AlpacaNormalizer(VenueId venue_id);

        // Parse a complete JSON message from the Alpaca WebSocket.
        // The message is a JSON array of objects. For each quote message,
        // update the internal book and invoke the callback.
        void on_message(const std::string &json_text, const BookCallback &callback);

        // Get current book for a symbol.
        [[nodiscard]] const OrderBook *get_book(const Symbol &symbol) const;

        // Number of symbols tracked.
        [[nodiscard]] size_t symbol_count() const noexcept { return books_.size(); }

    private:
        VenueId venue_id_;
        std::unordered_map<Symbol, OrderBook> books_;
        uint64_t sequence_{0};
    };

} // namespace sor::market_data::alpaca
