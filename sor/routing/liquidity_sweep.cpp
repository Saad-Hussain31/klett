#include "routing/liquidity_sweep.h"
#include "market_data/aggregator.h"
#include "market_data/book.h"
#include <algorithm>
#include <unordered_map>

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // LiquiditySweep::route
    //
    // Walk through aggregated book levels in price priority.  At each level,
    // allocate quantity to venues proportionally to their displayed liquidity.
    // Stop when the order is fully allocated or MAX_SLICES reached.
    // ---------------------------------------------------------------------------
    RoutingDecision LiquiditySweepStrategy::route(
        const Order &order,
        const market_data::NBBO &nbbo,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues)
    {
        RoutingDecision decision;

        if (venues.empty() || !nbbo.valid())
        {
            return decision;
        }

        Quantity remaining = order.leaves_qty();
        if (remaining <= 0)
        {
            return decision;
        }

        const bool is_buy = (order.side == Side::Buy);
        const auto &levels = is_buy ? book.asks : book.bids;
        const size_t depth = is_buy ? book.ask_depth : book.bid_depth;

        // Build a fast lookup of available venues.
        // We use venue_id -> index into `venues` vector.
        std::unordered_map<VenueId, size_t> venue_index;
        venue_index.reserve(venues.size());
        for (size_t i = 0; i < venues.size(); ++i)
        {
            if (venues[i].is_available)
            {
                venue_index[venues[i].venue_id] = i;
            }
        }

        // Accumulate per-venue allocations across multiple price levels.
        // A venue may appear at several levels; we merge into one slice per venue
        // at its worst (highest ask / lowest bid) price to keep slice count low.
        struct VenueAlloc
        {
            VenueId venue_id{0};
            Price worst_price{0}; // worst price we allocated at for this venue
            Quantity total_qty{0};
        };
        std::unordered_map<VenueId, VenueAlloc> allocations;

        for (size_t lvl = 0; lvl < depth && remaining > 0; ++lvl)
        {
            const auto &level = levels[lvl];
            if (level.price == INVALID_PRICE || level.total_quantity <= 0)
            {
                continue;
            }

            // Compute total available quantity from valid venues at this level.
            Quantity level_available = 0;
            for (size_t v = 0; v < level.venue_count; ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity > 0 && venue_index.count(vq.venue_id))
                {
                    level_available += vq.quantity;
                }
            }
            if (level_available <= 0)
            {
                continue;
            }

            // How much of our remaining quantity can this level satisfy?
            const Quantity level_fill = std::min(remaining, level_available);

            // Allocate proportionally to each venue's contribution.
            Quantity level_allocated = 0;
            for (size_t v = 0; v < level.venue_count && level_allocated < level_fill; ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity <= 0 || !venue_index.count(vq.venue_id))
                {
                    continue;
                }

                // Pro-rata share, but capped at what the venue actually has.
                Quantity venue_share;
                if (level_available > 0)
                {
                    // Use integer math carefully to avoid overflow.
                    venue_share = static_cast<Quantity>(
                        static_cast<double>(vq.quantity) / static_cast<double>(level_available) * static_cast<double>(level_fill));
                }
                else
                {
                    venue_share = 0;
                }

                // Clamp to venue's available quantity.
                venue_share = std::min(venue_share, vq.quantity);
                // Clamp to remaining needed at this level.
                venue_share = std::min(venue_share, level_fill - level_allocated);

                if (venue_share < min_slice_quantity_ && venue_share < (level_fill - level_allocated))
                {
                    // Skip dust allocations unless it is the remainder that completes the order.
                    continue;
                }

                auto &alloc = allocations[vq.venue_id];
                alloc.venue_id = vq.venue_id;
                alloc.worst_price = level.price; // overwritten each level -- keeps worst
                alloc.total_qty += venue_share;

                level_allocated += venue_share;
            }

            // Assign any rounding remainder to the venue with the most liquidity
            // at this level (avoids losing shares to rounding).
            if (level_allocated < level_fill)
            {
                Quantity remainder = level_fill - level_allocated;
                VenueId best_venue{0};
                Quantity best_qty{0};
                for (size_t v = 0; v < level.venue_count; ++v)
                {
                    const auto &vq = level.venue_breakdown[v];
                    if (vq.quantity > best_qty && venue_index.count(vq.venue_id))
                    {
                        best_qty = vq.quantity;
                        best_venue = vq.venue_id;
                    }
                }
                if (best_venue != 0)
                {
                    auto &alloc = allocations[best_venue];
                    alloc.venue_id = best_venue;
                    alloc.worst_price = level.price;
                    alloc.total_qty += remainder;
                    level_allocated += remainder;
                }
            }

            remaining -= level_allocated;
        }

        // Convert allocations to slices.
        decision.slices.reserve(std::min(allocations.size(), MAX_SLICES));
        for (const auto &[vid, alloc] : allocations)
        {
            if (alloc.total_qty < min_slice_quantity_)
            {
                continue; // drop dust
            }
            if (decision.slices.size() >= MAX_SLICES)
            {
                break;
            }

            RoutingDecision::Slice slice;
            slice.venue_id = alloc.venue_id;
            slice.price = alloc.worst_price;
            slice.quantity = alloc.total_qty;
            slice.type = OrderType::Limit;
            slice.tif = TimeInForce::IOC; // sweeps are always IOC

            decision.slices.push_back(slice);
        }

        // Sort slices by price (best first) for deterministic behavior.
        if (is_buy)
        {
            std::sort(decision.slices.begin(), decision.slices.end(),
                      [](const auto &a, const auto &b)
                      { return a.price < b.price; });
        }
        else
        {
            std::sort(decision.slices.begin(), decision.slices.end(),
                      [](const auto &a, const auto &b)
                      { return a.price > b.price; });
        }

        return decision;
    }

} // namespace sor::routing
