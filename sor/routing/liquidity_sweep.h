#pragma once

// LiquiditySweep routing strategy.
//
// Simultaneously sweeps liquidity across multiple venues, walking through
// the aggregated book from best to worst price and allocating quantity
// proportionally to available liquidity at each level.

#include "routing/strategy.h"

namespace sor::routing
{

    class LiquiditySweepStrategy final : public RoutingStrategy
    {
    public:
        /// Maximum number of venue slices in a single routing decision.
        static constexpr size_t MAX_SLICES = 10;

        /// Minimum quantity for any single slice.  Prevents "dust" orders that
        /// would waste gateway bandwidth and incur minimum-fee charges.
        static constexpr Quantity DEFAULT_MIN_SLICE_QTY = 1;

        explicit LiquiditySweepStrategy(Quantity min_slice_quantity = DEFAULT_MIN_SLICE_QTY) noexcept
            : min_slice_quantity_(min_slice_quantity) {}

        RoutingDecision route(const Order &order,
                              const market_data::NBBO &nbbo,
                              const market_data::AggregatedBook &book,
                              const std::vector<VenueScore> &venues) override;

        const char *name() const noexcept override { return "LiquiditySweep"; }
        sor::RoutingStrategy type() const noexcept override
        {
            return sor::RoutingStrategy::LiquiditySweep;
        }

        void set_min_slice_quantity(Quantity qty) noexcept { min_slice_quantity_ = qty; }
        [[nodiscard]] Quantity min_slice_quantity() const noexcept { return min_slice_quantity_; }

    private:
        Quantity min_slice_quantity_;
    };

} // namespace sor::routing
