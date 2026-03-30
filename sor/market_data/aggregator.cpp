#include "market_data/aggregator.h"

#include <algorithm>

namespace sor::market_data
{

    // ---------------------------------------------------------------------------
    // NBBO
    // ---------------------------------------------------------------------------

    Price NBBO::spread() const noexcept
    {
        if (best_bid == INVALID_PRICE || best_ask == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        return best_ask - best_bid;
    }

    Price NBBO::mid_price() const noexcept
    {
        if (best_bid == INVALID_PRICE || best_ask == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        return (best_bid + best_ask) / 2;
    }

    bool NBBO::valid() const noexcept
    {
        return best_bid != INVALID_PRICE && best_ask != INVALID_PRICE &&
               best_bid < best_ask && best_bid_qty > 0 && best_ask_qty > 0;
    }

    // ---------------------------------------------------------------------------
    // AggregatedBook::AggregatedLevel
    // ---------------------------------------------------------------------------

    void AggregatedBook::AggregatedLevel::add_venue(VenueId venue_id, Quantity qty)
    {
        if (venue_count < venue_breakdown.size())
        {
            venue_breakdown[venue_count] = {venue_id, qty};
            ++venue_count;
            total_quantity += qty;
        }
    }

    // ---------------------------------------------------------------------------
    // MarketDataAggregator
    // ---------------------------------------------------------------------------

    void MarketDataAggregator::register_venue(VenueId venue_id)
    {
        // Avoid duplicate registrations.
        for (auto v : venues_)
        {
            if (v == venue_id)
                return;
        }
        venues_.push_back(venue_id);
        venue_books_[venue_id]; // default-construct inner map
    }

    void MarketDataAggregator::on_book_update(VenueId venue_id, const Symbol &symbol,
                                              const OrderBook &book)
    {
        venue_books_[venue_id][symbol] = book;
        recalculate_nbbo(symbol);
    }

    NBBO MarketDataAggregator::get_nbbo(const Symbol &symbol) const
    {
        auto it = nbbo_cache_.find(symbol);
        if (it != nbbo_cache_.end())
        {
            return it->second;
        }
        return NBBO{};
    }

    AggregatedBook MarketDataAggregator::get_aggregated_book(const Symbol &symbol) const
    {
        AggregatedBook agg{};
        agg.symbol = symbol;
        agg.nbbo = get_nbbo(symbol);
        build_aggregated_book(symbol, agg);
        return agg;
    }

    const OrderBook *MarketDataAggregator::get_venue_book(VenueId venue_id,
                                                          const Symbol &symbol) const
    {
        auto venue_it = venue_books_.find(venue_id);
        if (venue_it == venue_books_.end())
            return nullptr;

        auto sym_it = venue_it->second.find(symbol);
        if (sym_it == venue_it->second.end())
            return nullptr;

        return &sym_it->second;
    }

    bool MarketDataAggregator::is_stale(const Symbol &symbol,
                                        std::chrono::microseconds max_age) const
    {
        const auto now = std::chrono::steady_clock::now();

        for (auto venue_id : venues_)
        {
            const OrderBook *book = get_venue_book(venue_id, symbol);
            if (book && book->last_update != Timestamp{})
            {
                auto age = std::chrono::duration_cast<std::chrono::microseconds>(
                    now - book->last_update);
                if (age <= max_age)
                {
                    return false; // At least one venue is fresh.
                }
            }
        }
        return true; // All venues are stale or absent.
    }

    void MarketDataAggregator::set_nbbo_callback(NBBOCallback cb)
    {
        nbbo_callback_ = std::move(cb);
    }

    void MarketDataAggregator::recalculate_nbbo(const Symbol &symbol)
    {
        NBBO nbbo{};
        nbbo.timestamp = std::chrono::steady_clock::now();

        for (auto venue_id : venues_)
        {
            const OrderBook *book = get_venue_book(venue_id, symbol);
            if (!book)
                continue;

            // Best bid: highest across venues.
            Price bb = book->best_bid();
            if (bb != INVALID_PRICE)
            {
                if (nbbo.best_bid == INVALID_PRICE || bb > nbbo.best_bid)
                {
                    nbbo.best_bid = bb;
                    nbbo.best_bid_qty = book->bids.best().quantity;
                    nbbo.best_bid_venue = venue_id;
                }
                else if (bb == nbbo.best_bid)
                {
                    // Same price -- accumulate quantity, keep first venue.
                    nbbo.best_bid_qty += book->bids.best().quantity;
                }
            }

            // Best ask: lowest across venues.
            Price ba = book->best_ask();
            if (ba != INVALID_PRICE)
            {
                if (nbbo.best_ask == INVALID_PRICE || ba < nbbo.best_ask)
                {
                    nbbo.best_ask = ba;
                    nbbo.best_ask_qty = book->asks.best().quantity;
                    nbbo.best_ask_venue = venue_id;
                }
                else if (ba == nbbo.best_ask)
                {
                    nbbo.best_ask_qty += book->asks.best().quantity;
                }
            }
        }

        NBBO old_nbbo = nbbo_cache_[symbol];
        nbbo_cache_[symbol] = nbbo;

        // Fire callback if NBBO changed.
        if (nbbo_callback_ &&
            (old_nbbo.best_bid != nbbo.best_bid ||
             old_nbbo.best_ask != nbbo.best_ask ||
             old_nbbo.best_bid_qty != nbbo.best_bid_qty ||
             old_nbbo.best_ask_qty != nbbo.best_ask_qty))
        {
            nbbo_callback_(symbol, nbbo);
        }
    }

    void MarketDataAggregator::build_aggregated_book(const Symbol &symbol,
                                                     AggregatedBook &out) const
    {
        // Collect all bid levels across venues with attribution.
        struct TaggedLevel
        {
            Price price;
            Quantity quantity;
            VenueId venue_id;
        };

        std::vector<TaggedLevel> all_bids;
        std::vector<TaggedLevel> all_asks;

        for (auto venue_id : venues_)
        {
            const OrderBook *book = get_venue_book(venue_id, symbol);
            if (!book)
                continue;

            for (size_t i = 0; i < book->bids.depth; ++i)
            {
                const auto &lvl = book->bids.levels[i];
                if (lvl.valid())
                {
                    all_bids.push_back({lvl.price, lvl.quantity, venue_id});
                }
            }
            for (size_t i = 0; i < book->asks.depth; ++i)
            {
                const auto &lvl = book->asks.levels[i];
                if (lvl.valid())
                {
                    all_asks.push_back({lvl.price, lvl.quantity, venue_id});
                }
            }
        }

        // Sort bids descending by price.
        std::sort(all_bids.begin(), all_bids.end(),
                  [](const TaggedLevel &a, const TaggedLevel &b)
                  {
                      return a.price > b.price;
                  });

        // Sort asks ascending by price.
        std::sort(all_asks.begin(), all_asks.end(),
                  [](const TaggedLevel &a, const TaggedLevel &b)
                  {
                      return a.price < b.price;
                  });

        // Merge into aggregated levels (group by price).
        auto merge = [](const std::vector<TaggedLevel> &sorted,
                        std::array<AggregatedBook::AggregatedLevel, MAX_DEPTH> &out_levels,
                        size_t &out_depth)
        {
            out_depth = 0;
            for (const auto &tl : sorted)
            {
                if (out_depth > 0 && out_levels[out_depth - 1].price == tl.price)
                {
                    // Same price -- add venue to existing level.
                    out_levels[out_depth - 1].add_venue(tl.venue_id, tl.quantity);
                }
                else
                {
                    if (out_depth >= MAX_DEPTH)
                        break;
                    auto &lvl = out_levels[out_depth];
                    lvl.price = tl.price;
                    lvl.total_quantity = 0;
                    lvl.venue_count = 0;
                    lvl.add_venue(tl.venue_id, tl.quantity);
                    ++out_depth;
                }
            }
        };

        merge(all_bids, out.bids, out.bid_depth);
        merge(all_asks, out.asks, out.ask_depth);
    }

} // namespace sor::market_data
