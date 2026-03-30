#include "routing/vwap.h"
#include "market_data/aggregator.h"
#include "market_data/book.h"
#include <algorithm>
#include <cmath>

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------
    VWAPStrategy::VWAPStrategy() noexcept
        : config_() {}

    VWAPStrategy::VWAPStrategy(const Config &cfg) noexcept
        : config_(cfg) {}

    // ---------------------------------------------------------------------------
    // begin -- reset state for a new parent order.
    // ---------------------------------------------------------------------------
    void VWAPStrategy::begin(Quantity total_quantity, Timestamp start_time)
    {
        total_quantity_ = total_quantity;
        filled_so_far_ = 0;
        slices_sent_ = 0;
        start_time_ = start_time;
        end_time_ = start_time + config_.duration;
    }

    // ---------------------------------------------------------------------------
    // on_fill -- record fills against the schedule.
    // ---------------------------------------------------------------------------
    void VWAPStrategy::on_fill(Quantity filled_qty) noexcept
    {
        filled_so_far_ += filled_qty;
        if (filled_so_far_ > total_quantity_)
        {
            filled_so_far_ = total_quantity_; // clamp
        }
    }

    // ---------------------------------------------------------------------------
    // Progress metrics
    // ---------------------------------------------------------------------------
    double VWAPStrategy::fill_progress() const noexcept
    {
        if (total_quantity_ <= 0)
            return 1.0;
        return static_cast<double>(filled_so_far_) / static_cast<double>(total_quantity_);
    }

    double VWAPStrategy::time_progress() const noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= end_time_)
            return 1.0;
        if (now <= start_time_)
            return 0.0;
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - start_time_);
        const auto total = std::chrono::duration_cast<std::chrono::microseconds>(end_time_ - start_time_);
        if (total.count() <= 0)
            return 1.0;
        return static_cast<double>(elapsed.count()) / static_cast<double>(total.count());
    }

    double VWAPStrategy::participation_rate() const noexcept
    {
        const double tp = time_progress();
        if (tp <= 0.0)
            return 0.0;
        return fill_progress() / tp;
    }

    bool VWAPStrategy::is_complete() const noexcept
    {
        return filled_so_far_ >= total_quantity_;
    }

    // ---------------------------------------------------------------------------
    // compute_slice_quantity
    //
    // The base slice quantity is total_quantity / num_slices.  We then adjust:
    //   - If behind schedule (fill_progress < time_progress), accelerate by
    //     sending a larger slice.  The multiplier is proportional to urgency.
    //   - If ahead of schedule, throttle by sending a smaller slice.
    //   - Urgency=1.0 doubles the catch-up multiplier.
    //   - Always respect max_participation_rate as a ceiling.
    // ---------------------------------------------------------------------------
    Quantity VWAPStrategy::compute_slice_quantity() const noexcept
    {
        if (total_quantity_ <= 0 || config_.num_slices == 0)
        {
            return 0;
        }

        const Quantity remaining = total_quantity_ - filled_so_far_;
        if (remaining <= 0)
        {
            return 0;
        }

        const Quantity base_slice =
            std::max<Quantity>(1, total_quantity_ / static_cast<Quantity>(config_.num_slices));

        const double tp = time_progress();
        const double fp = fill_progress();

        // How far behind/ahead we are as a ratio.
        // Positive = behind schedule, negative = ahead.
        const double drift = tp - fp;

        // Compute acceleration factor.
        // At urgency=0 the factor stays near 1.0.
        // At urgency=1 and 50% behind, factor = 1 + 1.0*0.5*2 = 2.0 (double pace).
        constexpr double kMaxAccel = 3.0;
        constexpr double kMinDecel = 0.25;
        double accel = 1.0;
        if (drift > 0.0)
        {
            // Behind schedule -- speed up.
            accel = 1.0 + config_.urgency * drift * 2.0;
            accel = std::min(accel, kMaxAccel);
        }
        else
        {
            // Ahead of schedule -- slow down (only if not fully urgent).
            accel = 1.0 + (1.0 - config_.urgency) * drift; // drift is negative
            accel = std::max(accel, kMinDecel);
        }

        auto slice_qty = static_cast<Quantity>(
            static_cast<double>(base_slice) * accel);

        // Apply participation rate cap.
        if (config_.max_participation_rate > 0.0 && config_.max_participation_rate < 1.0)
        {
            const Quantity max_qty = static_cast<Quantity>(
                static_cast<double>(total_quantity_) * config_.max_participation_rate);
            slice_qty = std::min(slice_qty, std::max<Quantity>(1, max_qty));
        }

        // Never exceed remaining.
        slice_qty = std::min(slice_qty, remaining);
        return std::max<Quantity>(1, slice_qty);
    }

    // ---------------------------------------------------------------------------
    // route -- standard interface entry point.
    // Delegates to get_next_slice.
    // ---------------------------------------------------------------------------
    RoutingDecision VWAPStrategy::route(
        const Order &order,
        const market_data::NBBO &nbbo,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues)
    {
        // If begin() was never called, auto-initialize from the order.
        if (total_quantity_ == 0 && order.leaves_qty() > 0)
        {
            begin(order.quantity, std::chrono::steady_clock::now());
        }
        return get_next_slice(order, nbbo, book, venues);
    }

    // ---------------------------------------------------------------------------
    // get_next_slice -- VWAP-specific slice generation.
    //
    // 1. Determine target quantity for this time slice.
    // 2. Use BestPrice-style venue selection for the slice.
    // ---------------------------------------------------------------------------
    RoutingDecision VWAPStrategy::get_next_slice(
        const Order &order,
        const market_data::NBBO &nbbo,
        const market_data::AggregatedBook &book,
        const std::vector<VenueScore> &venues)
    {
        RoutingDecision decision;

        if (is_complete() || venues.empty() || !nbbo.valid())
        {
            return decision;
        }

        const Quantity slice_qty = compute_slice_quantity();
        if (slice_qty <= 0)
        {
            return decision;
        }

        // --- BestPrice venue selection for this slice ---
        const bool is_buy = (order.side == Side::Buy);
        const auto &levels = is_buy ? book.asks : book.bids;
        const size_t depth = is_buy ? book.ask_depth : book.bid_depth;

        struct Candidate
        {
            VenueId venue_id{0};
            Price price{INVALID_PRICE};
            double quality{-1.0};
            bool found{false};
        } best;

        for (size_t lvl = 0; lvl < depth; ++lvl)
        {
            const auto &level = levels[lvl];
            if (level.price == INVALID_PRICE || level.total_quantity <= 0)
            {
                continue;
            }

            for (size_t v = 0; v < level.venue_count; ++v)
            {
                const auto &vq = level.venue_breakdown[v];
                if (vq.quantity <= 0)
                {
                    continue;
                }

                // Find VenueScore.
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

                // Fee-adjusted effective price.
                const double raw = to_double(level.price);
                Price adj;
                if (is_buy)
                {
                    adj = to_price(raw * (1.0 + score->fee_rate));
                }
                else
                {
                    adj = to_price(raw * (1.0 - score->fee_rate));
                }

                // Quality: latency + fill rate.
                constexpr double kBaseline = 1000.0;
                const double lat = score->latency_us > 0.0 ? score->latency_us : kBaseline;
                const double quality = 0.7 * score->fill_rate + 0.3 * (kBaseline / lat);

                bool is_better = false;
                if (!best.found)
                {
                    is_better = true;
                }
                else if (is_buy)
                {
                    if (adj < to_price(to_double(best.price) * (1.0 + score->fee_rate)))
                    {
                        is_better = true;
                    }
                    else if (adj == to_price(to_double(best.price) * (1.0 + score->fee_rate)) && quality > best.quality)
                    {
                        is_better = true;
                    }
                }
                else
                {
                    if (adj > to_price(to_double(best.price) * (1.0 - score->fee_rate)))
                    {
                        is_better = true;
                    }
                    else if (adj == to_price(to_double(best.price) * (1.0 - score->fee_rate)) && quality > best.quality)
                    {
                        is_better = true;
                    }
                }

                if (is_better)
                {
                    best.venue_id = vq.venue_id;
                    best.price = level.price;
                    best.quality = quality;
                    best.found = true;
                }
            }

            // For VWAP we only need the top-of-book venue; don't chase worse
            // levels unless urgency is high and we are behind schedule.
            if (best.found && config_.urgency < 0.8)
            {
                break;
            }
        }

        if (!best.found)
        {
            return decision;
        }

        RoutingDecision::Slice slice;
        slice.venue_id = best.venue_id;
        slice.price = best.price;
        slice.quantity = slice_qty;
        slice.type = OrderType::Limit;
        slice.tif = TimeInForce::IOC;

        decision.slices.push_back(slice);
        ++slices_sent_;
        return decision;
    }

} // namespace sor::routing
