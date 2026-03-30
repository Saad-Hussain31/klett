#include "market_data/book.h"

#include <cstring>

namespace sor::market_data
{

    // ---------------------------------------------------------------------------
    // BookSide
    // ---------------------------------------------------------------------------

    void BookSide::update(Price price, Quantity qty, int32_t order_count)
    {
        // Search for existing level with matching price.
        for (size_t i = 0; i < depth; ++i)
        {
            if (levels[i].price == price)
            {
                if (qty > 0)
                {
                    // Update in-place.
                    levels[i].quantity = qty;
                    levels[i].order_count = order_count;
                }
                else
                {
                    // Quantity zero means remove.
                    remove(price);
                }
                return;
            }
        }

        // Not found -- insert if qty > 0.
        if (qty > 0)
        {
            PriceLevel level{price, qty, order_count};
            // Caller must know whether this is a bid or ask side for correct
            // insertion order.  We infer from the current ordering: if the book
            // has at least two levels, we can tell.  Otherwise, we just append.
            // However, the canonical call site should use insert_sorted directly
            // when the side is known.  As a convenience the update() function
            // here detects the sort direction from existing data.
            if (depth >= 2)
            {
                bool is_bid = levels[0].price > levels[1].price;
                insert_sorted(level, is_bid);
            }
            else if (depth == 1)
            {
                // With only one level, we cannot infer direction.  Place by
                // comparison: treat higher price first as bid-like.
                insert_sorted(level, price > levels[0].price || levels[0].price > price);
            }
            else
            {
                // Empty book -- just place it.
                levels[0] = level;
                depth = 1;
            }
        }
    }

    void BookSide::insert_sorted(const PriceLevel &level, bool is_bid)
    {
        if (depth >= MAX_DEPTH)
        {
            // Book is full.  Only insert if this level is better than the worst.
            const auto &worst = levels[depth - 1];
            bool better = is_bid ? (level.price > worst.price)
                                 : (level.price < worst.price);
            if (!better)
            {
                return; // Discard -- beyond max depth.
            }
            // Evict the worst level and fall through to insert.
            --depth;
        }

        // Find insertion point.
        size_t pos = 0;
        if (is_bid)
        {
            // Bids: descending by price (highest first).
            while (pos < depth && levels[pos].price > level.price)
            {
                ++pos;
            }
        }
        else
        {
            // Asks: ascending by price (lowest first).
            while (pos < depth && levels[pos].price < level.price)
            {
                ++pos;
            }
        }

        // Check for duplicate price at insertion point.
        if (pos < depth && levels[pos].price == level.price)
        {
            levels[pos] = level;
            return;
        }

        // Shift elements to make room.
        if (depth < MAX_DEPTH)
        {
            for (size_t i = depth; i > pos; --i)
            {
                levels[i] = levels[i - 1];
            }
            levels[pos] = level;
            ++depth;
        }
    }

    void BookSide::remove(Price price)
    {
        for (size_t i = 0; i < depth; ++i)
        {
            if (levels[i].price == price)
            {
                // Shift remaining levels down.
                for (size_t j = i; j + 1 < depth; ++j)
                {
                    levels[j] = levels[j + 1];
                }
                // Clear the last slot.
                levels[depth - 1] = PriceLevel{};
                --depth;
                return;
            }
        }
    }

    void BookSide::clear() noexcept
    {
        for (size_t i = 0; i < depth; ++i)
        {
            levels[i] = PriceLevel{};
        }
        depth = 0;
    }

    // ---------------------------------------------------------------------------
    // OrderBook
    // ---------------------------------------------------------------------------

    Price OrderBook::mid_price() const noexcept
    {
        const Price bb = best_bid();
        const Price ba = best_ask();
        if (bb == INVALID_PRICE || ba == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        return (bb + ba) / 2;
    }

    Price OrderBook::spread() const noexcept
    {
        const Price bb = best_bid();
        const Price ba = best_ask();
        if (bb == INVALID_PRICE || ba == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        return ba - bb;
    }

    Quantity OrderBook::quantity_at_or_better(Side side, Price price) const noexcept
    {
        Quantity total = 0;

        if (side == Side::Buy)
        {
            // Bids at or better (higher) than price.
            for (size_t i = 0; i < bids.depth; ++i)
            {
                if (bids.levels[i].price >= price)
                {
                    total += bids.levels[i].quantity;
                }
                else
                {
                    break; // Bids are descending, no further matches.
                }
            }
        }
        else
        {
            // Asks at or better (lower) than price.
            for (size_t i = 0; i < asks.depth; ++i)
            {
                if (asks.levels[i].price <= price)
                {
                    total += asks.levels[i].quantity;
                }
                else
                {
                    break; // Asks are ascending, no further matches.
                }
            }
        }

        return total;
    }

    bool OrderBook::is_crossed() const noexcept
    {
        const Price bb = best_bid();
        const Price ba = best_ask();
        if (bb == INVALID_PRICE || ba == INVALID_PRICE)
        {
            return false;
        }
        return bb >= ba;
    }

    void OrderBook::clear() noexcept
    {
        bids.clear();
        asks.clear();
        last_update = {};
        sequence = 0;
    }

} // namespace sor::market_data
