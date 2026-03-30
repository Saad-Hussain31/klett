#include "routing/smart_ioc.h"
#include "market_data/aggregator.h"
#include "market_data/book.h"
#include <algorithm>
#include <unordered_map>

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // Compute the worst acceptable price given NBBO and slippage tolerance.
    //
    // For buys:  worst = best_ask + slippage_ticks   (willing to pay more)
    // For sells: worst = best_bid - slippage_ticks   (willing to accept less)
    // ---------------------------------------------------------------------------
    static Price worst_acceptable_price(const market_data::NBBO &nbbo,
                                        Side side,
                                        int64_t slippage_ticks) noexcept
    {
        if (side == Side::Buy)
        {
            if (nbbo.best_ask == INVALID_PRICE)
            {
                return INVALID_PRICE;
            }
            return nbbo.best_ask + slippage_ticks;
        }
        // Sell
        if (nbbo.best_bid == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        const Price worst = nbbo.best_bid - slippage_ticks;
        return worst > 0 ? worst : 1; // price must remain positive
    }

    // ---------------------------------------------------------------------------
    // Latency-weighted quality for IOC venue preference.
    // Pure latency focus for time-sensitive orders.
    // ---------------------------------------------------------------------------
    static double latency_quality(const VenueScore &vs) noexcept
    {
        constexpr double kBaseline = 1000.0; // 1ms baseline
        const double lat = vs.latency_us > 0.0 ? vs.latency_us : kBaseline;
        // Invert: lower latency -> higher score.  Blend with fill rate.
        return 0.4 * vs.fill_rate + 0.6 * (kBaseline / lat);
    }

    // ---------------------------------------------------------------------------
    // SmartIOC::route
    // ---------------------------------------------------------------------------
    RoutingDecision SmartIOCStrategy::route(
        const Order &order,
        const market_data::NBBO &nbbo,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues)
    {
        if (venues.empty() || !nbbo.valid())
        {
            return {};
        }

        const Quantity qty = order.leaves_qty();
        if (qty <= 0)
        {
            return {};
        }

        const Price worst = worst_acceptable_price(nbbo, order.side, slippage_ticks_);
        if (worst == INVALID_PRICE)
        {
            return {};
        }

        // FOK semantics: must fill entirely at one venue or reject.
        if (order.tif == TimeInForce::FOK || order.type == OrderType::FOK)
        {
            return try_fok(order, book, venues, worst);
        }

        // IOC semantics: sweep aggressively across venues.
        return sweep_ioc(order, book, venues, worst);
    }

    // ---------------------------------------------------------------------------
    // FOK: find a single venue with enough liquidity at acceptable prices.
    // ---------------------------------------------------------------------------
    RoutingDecision SmartIOCStrategy::try_fok(
        const Order &order,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues,
        Price worst_acceptable) const
    {
        RoutingDecision decision;
        const Quantity qty = order.leaves_qty();
        const bool is_buy = (order.side == Side::Buy);
        const auto &levels = is_buy ? book.asks : book.bids;
        const size_t depth = is_buy ? book.ask_depth : book.bid_depth;

        // For each venue, compute total quantity available within the slippage band.
        struct VenueCandidate
        {
            VenueId venue_id{0};
            Quantity total_available{0};
            Price worst_price{0};
            double quality{0.0};
        };

        std::unordered_map<VenueId, VenueCandidate> candidates;

        for (size_t lvl = 0; lvl < depth; ++lvl)
        {
            const auto &level = levels[lvl];
            if (level.price == INVALID_PRICE || level.total_quantity <= 0)
            {
                continue;
            }

            // Check if this price level is within our slippage tolerance.
            if (is_buy && level.price > worst_acceptable)
            {
                break; // asks are sorted ascending; no point continuing
            }
            if (!is_buy && level.price < worst_acceptable)
            {
                break; // bids are sorted descending
            }

            for (size_t v = 0; v < level.venue_count; ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity <= 0)
                {
                    continue;
                }

                // Verify venue is available.
                bool available = false;
                double q = 0.0;
                for (const auto &vs : venues)
                {
                    if (vs.venue_id == vq.venue_id && vs.is_available)
                    {
                        available = true;
                        q = latency_quality(vs);
                        break;
                    }
                }
                if (!available)
                {
                    continue;
                }

                auto &cand = candidates[vq.venue_id];
                cand.venue_id = vq.venue_id;
                cand.total_available += vq.quantity;
                cand.worst_price = level.price; // updates as we go deeper
                cand.quality = q;
            }
        }

        // Find the best venue that can fill the entire order.
        const VenueCandidate *best = nullptr;
        for (const auto &[vid, cand] : candidates)
        {
            if (cand.total_available < qty)
            {
                continue; // cannot fill entirely
            }
            if (!best || cand.quality > best->quality)
            {
                best = &cand;
            }
        }

        if (!best)
        {
            return decision; // no single venue can fill -- FOK rejects
        }

        RoutingDecision::Slice slice;
        slice.venue_id = best->venue_id;
        slice.price = best->worst_price;
        slice.quantity = qty;
        slice.type = OrderType::Limit;
        slice.tif = TimeInForce::FOK;

        decision.slices.push_back(slice);
        return decision;
    }

    // ---------------------------------------------------------------------------
    // IOC sweep with slippage tolerance.
    // Prefers low-latency venues.  At each price level, sorts venues by
    // latency quality before allocating.
    // ---------------------------------------------------------------------------
    RoutingDecision SmartIOCStrategy::sweep_ioc(
        const Order &order,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues,
        Price worst_acceptable) const
    {
        RoutingDecision decision;
        Quantity remaining = order.leaves_qty();
        const bool is_buy = (order.side == Side::Buy);
        const auto &levels = is_buy ? book.asks : book.bids;
        const size_t depth = is_buy ? book.ask_depth : book.bid_depth;

        // Build venue lookup.
        std::unordered_map<VenueId, const VenueScore *> venue_map;
        for (const auto &vs : venues)
        {
            if (vs.is_available)
            {
                venue_map[vs.venue_id] = &vs;
            }
        }

        // Per-venue accumulated allocation.
        struct Alloc
        {
            Price worst_price{0};
            Quantity total_qty{0};
        };
        std::unordered_map<VenueId, Alloc> allocations;

        for (size_t lvl = 0; lvl < depth && remaining > 0; ++lvl)
        {
            const auto &level = levels[lvl];
            if (level.price == INVALID_PRICE || level.total_quantity <= 0)
            {
                continue;
            }

            // Enforce slippage limit.
            if (is_buy && level.price > worst_acceptable)
            {
                break;
            }
            if (!is_buy && level.price < worst_acceptable)
            {
                break;
            }

            // Collect venue contributions at this level, sorted by latency quality.
            struct LevelVenue
            {
                VenueId venue_id;
                Quantity available;
                double quality;
            };

            // Use a small fixed-size array to avoid heap allocation in the hot path.
            std::array<LevelVenue, 16> lv_buf{};
            size_t lv_count = 0;

            for (size_t v = 0; v < level.venue_count && lv_count < lv_buf.size(); ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity <= 0)
                {
                    continue;
                }
                auto it = venue_map.find(vq.venue_id);
                if (it == venue_map.end())
                {
                    continue;
                }
                lv_buf[lv_count++] = {vq.venue_id, vq.quantity, latency_quality(*it->second)};
            }

            // Sort by quality descending (best venue first).
            std::sort(lv_buf.begin(), lv_buf.begin() + lv_count,
                      [](const LevelVenue &a, const LevelVenue &b)
                      {
                          return a.quality > b.quality;
                      });

            // Greedily allocate to the fastest/best venue first.
            for (size_t i = 0; i < lv_count && remaining > 0; ++i)
            {
                const auto &lv = lv_buf[i];
                const Quantity fill = std::min(remaining, lv.available);

                auto &alloc = allocations[lv.venue_id];
                alloc.worst_price = level.price;
                alloc.total_qty += fill;
                remaining -= fill;
            }
        }

        // Convert to slices.
        decision.slices.reserve(std::min(allocations.size(), MAX_SLICES));
        for (const auto &[vid, alloc] : allocations)
        {
            if (alloc.total_qty <= 0)
            {
                continue;
            }
            if (decision.slices.size() >= MAX_SLICES)
            {
                break;
            }

            RoutingDecision::Slice slice;
            slice.venue_id = vid;
            slice.price = alloc.worst_price;
            slice.quantity = alloc.total_qty;
            slice.type = OrderType::Limit;
            slice.tif = TimeInForce::IOC;

            decision.slices.push_back(slice);
        }

        // Sort slices by price (best first).
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
