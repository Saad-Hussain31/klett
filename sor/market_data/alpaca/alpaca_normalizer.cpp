#include "market_data/alpaca/alpaca_normalizer.h"
#include "infra/logging.h"

#include <nlohmann/json.hpp>
#include <chrono>

namespace sor::market_data::alpaca
{

    AlpacaNormalizer::AlpacaNormalizer(VenueId venue_id)
        : venue_id_(venue_id)
    {
    }

    void AlpacaNormalizer::on_message(const std::string &json_text, const BookCallback &callback)
    {
        nlohmann::json msgs;
        try
        {
            msgs = nlohmann::json::parse(json_text);
        }
        catch (const nlohmann::json::parse_error &)
        {
            SOR_LOG_WARN("[AlpacaNormalizer] Failed to parse JSON message");
            return;
        }

        if (!msgs.is_array())
            return;

        for (const auto &msg : msgs)
        {
            if (!msg.contains("T") || !msg["T"].is_string())
                continue;

            const auto &type = msg["T"].get_ref<const std::string &>();

            if (type != "q")
                continue;

            // Required fields: S (symbol), bp (bid price), bs (bid size),
            //                   ap (ask price), as (ask size)
            if (!msg.contains("S") || !msg.contains("bp") ||
                !msg.contains("bs") || !msg.contains("ap") ||
                !msg.contains("as"))
            {
                continue;
            }

            const std::string &sym_str = msg["S"].get_ref<const std::string &>();
            if (sym_str.empty() || sym_str.size() > 15)
                continue;

            Symbol symbol(sym_str);

            double bid_price = msg["bp"].get<double>();
            int64_t bid_size = msg["bs"].get<int64_t>();
            double ask_price = msg["ap"].get<double>();
            int64_t ask_size = msg["as"].get<int64_t>();

            // Skip invalid quotes
            if (bid_price <= 0.0 || ask_price <= 0.0 || bid_size <= 0 || ask_size <= 0)
                continue;

            // Build/update the internal book for this symbol
            auto &book = books_[symbol];
            book.symbol = symbol;
            book.venue_id = venue_id_;
            book.sequence = ++sequence_;
            book.last_update = std::chrono::steady_clock::now();

            // Clear and set single-level book (Alpaca free tier = top-of-book only)
            book.bids.clear();
            book.asks.clear();

            PriceLevel bid_level;
            bid_level.price = to_price(bid_price);
            bid_level.quantity = bid_size;
            bid_level.order_count = 1;

            PriceLevel ask_level;
            ask_level.price = to_price(ask_price);
            ask_level.quantity = ask_size;
            ask_level.order_count = 1;

            book.bids.levels[0] = bid_level;
            book.bids.depth = 1;
            book.asks.levels[0] = ask_level;
            book.asks.depth = 1;

            if (callback)
            {
                callback(venue_id_, symbol, book);
            }
        }
    }

    const OrderBook *AlpacaNormalizer::get_book(const Symbol &symbol) const
    {
        auto it = books_.find(symbol);
        if (it != books_.end())
            return &it->second;
        return nullptr;
    }

} // namespace sor::market_data::alpaca
