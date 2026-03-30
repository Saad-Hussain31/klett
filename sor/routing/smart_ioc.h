#pragma once

// SmartIOC routing strategy.
//
// Optimized for Immediate-or-Cancel and Fill-or-Kill orders.
// - FOK: only routes if a single venue can fill the entire quantity.
// - IOC: aggressively sweeps available liquidity, tolerating a configurable
//   amount of slippage beyond NBBO.
// Latency-sensitive: prefers faster venues for time-critical orders.

#include "routing/strategy.h"

namespace sor::routing
{

    class SmartIOCStrategy final : public RoutingStrategy
    {
    public:
        /// Maximum ticks (price increments) away from NBBO that we are willing
        /// to accept.  1 tick = 1 unit in the Price fixed-point representation.
        /// Callers should set this in terms of PRICE_SCALE-based ticks.
        static constexpr int64_t DEFAULT_SLIPPAGE_TICKS = 5;

        /// Maximum slices (same cap as LiquiditySweep).
        static constexpr size_t MAX_SLICES = 10;

        explicit SmartIOCStrategy(int64_t slippage_ticks = DEFAULT_SLIPPAGE_TICKS) noexcept
            : slippage_ticks_(slippage_ticks) {}

        RoutingDecision route(const Order &order,
                              const market_data::NBBO &nbbo,
                              const market_data::AggregatedBook &book,
                              const std::vector<VenueScore> &venues) override;

        const char *name() const noexcept override { return "SmartIOC"; }
        sor::RoutingStrategy type() const noexcept override
        {
            return sor::RoutingStrategy::SmartIOC;
        }

        void set_slippage_ticks(int64_t ticks) noexcept { slippage_ticks_ = ticks; }
        [[nodiscard]] int64_t slippage_ticks() const noexcept { return slippage_ticks_; }

    private:
        // Try FOK routing: find a single venue that can fill the entire order.
        RoutingDecision try_fok(const Order &order,
                                const market_data::AggregatedBook &book,
                                const std::vector<VenueScore> &venues,
                                Price worst_acceptable) const;

        // IOC sweep with slippage tolerance, preferring low-latency venues.
        RoutingDecision sweep_ioc(const Order &order,
                                  const market_data::AggregatedBook &book,
                                  const std::vector<VenueScore> &venues,
                                  Price worst_acceptable) const;

        int64_t slippage_ticks_;
    };

} // namespace sor::routing
