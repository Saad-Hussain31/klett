#include "routing/best_price.h"
#include "market_data/aggregator.h"
#include "market_data/book.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // Fee-adjusted effective price.
    //
    // For a BUY  the effective cost  = price * (1 + fee_rate)  -- higher is worse.
    // For a SELL the effective yield = price * (1 - fee_rate)  -- lower is worse.
    // ---------------------------------------------------------------------------
    static Price fee_adjusted(Price raw_price, double fee_rate, Side side) noexcept
    {
        if (raw_price == INVALID_PRICE)
        {
            return INVALID_PRICE;
        }
        const double raw = to_double(raw_price);
        if (side == Side::Buy)
        {
            return to_price(raw * (1.0 + fee_rate));
        }
        return to_price(raw * (1.0 - fee_rate));
    }

    // ---------------------------------------------------------------------------
    // Venue quality score -- used as a tie-breaker when fee-adjusted prices are
    // identical.  Combines latency (lower is better) and fill rate (higher is
    // better) into a single number.  Higher quality value == better venue.
    // ---------------------------------------------------------------------------
    double BestPriceStrategy::compute_venue_quality(const VenueScore &vs) noexcept
    {
        // Normalize latency contribution: assume 1000 us is a "normal" value.
        // Venues faster than that get a bonus, slower get a penalty.
        constexpr double kLatencyBaseline = 1000.0;
        const double latency_score =
            kLatencyBaseline / (vs.latency_us > 0.0 ? vs.latency_us : kLatencyBaseline);

        // Weighted combination.  Fill rate is the dominant factor.
        constexpr double kFillWeight = 0.7;
        constexpr double kLatencyWeight = 0.3;
        return kFillWeight * vs.fill_rate + kLatencyWeight * latency_score;
    }

    // ---------------------------------------------------------------------------
    // BestPrice::route
    // ---------------------------------------------------------------------------
    RoutingDecision BestPriceStrategy::route(
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

        const Quantity qty = order.leaves_qty();
        if (qty <= 0)
        {
            return decision;
        }

        // Determine which side of the book to look at.
        const bool is_buy = (order.side == Side::Buy);

        // Walk the aggregated book levels to find the venue with the best
        // fee-adjusted price that has sufficient (or partial) liquidity.
        const auto &levels = is_buy ? book.asks : book.bids;
        const size_t depth = is_buy ? book.ask_depth : book.bid_depth;

        // Track the best venue found so far.
        struct Candidate
        {
            VenueId venue_id{0};
            Price fee_adj_price{INVALID_PRICE};
            Price raw_price{INVALID_PRICE};
            double quality{-1.0};
            Quantity available{0};
            bool found{false};
        } best;

        for (size_t lvl = 0; lvl < depth; ++lvl)
        {
            const auto &level = levels[lvl];
            if (level.price == INVALID_PRICE || level.total_quantity <= 0)
            {
                continue;
            }

            // Check each venue contributing to this price level.
            for (size_t v = 0; v < level.venue_count; ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity <= 0)
                {
                    continue;
                }

                // Find corresponding VenueScore.
                const VenueScore *score = nullptr;
                for (const auto &vs : venues)
                {
                    if (vs.venue_id == vq.venue_id && vs.is_available)
                    {
                        score = &vs;
                        break;
                    }
                }
                if (!score)
                {
                    continue;
                }

                const Price adj = fee_adjusted(level.price, score->fee_rate, order.side);
                const double quality = compute_venue_quality(*score);

                // Compare with current best.
                bool is_better = false;
                if (!best.found)
                {
                    is_better = true;
                }
                else if (is_buy)
                {
                    // Buy: lower fee-adjusted ask is better.
                    if (adj < best.fee_adj_price)
                    {
                        is_better = true;
                    }
                    else if (adj == best.fee_adj_price && quality > best.quality)
                    {
                        is_better = true;
                    }
                }
                else
                {
                    // Sell: higher fee-adjusted bid is better.
                    if (adj > best.fee_adj_price)
                    {
                        is_better = true;
                    }
                    else if (adj == best.fee_adj_price && quality > best.quality)
                    {
                        is_better = true;
                    }
                }

                if (is_better)
                {
                    best.venue_id = vq.venue_id;
                    best.fee_adj_price = adj;
                    best.raw_price = level.price;
                    best.quality = quality;
                    best.available = vq.quantity;
                    best.found = true;
                }
            }
        }

        if (!best.found)
        {
            return decision;
        }

        // Route entire order to the best venue (exchange handles partial if qty
        // exceeds available).
        RoutingDecision::Slice slice;
        slice.venue_id = best.venue_id;
        slice.price = best.raw_price;
        slice.quantity = qty;
        slice.type = order.type;
        slice.tif = order.tif;

        decision.slices.push_back(slice);
        return decision;
    }

} // namespace sor::routing
