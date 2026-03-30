#pragma once

// BestPrice routing strategy.
//
// Routes the entire order to the single venue offering the best
// fee-adjusted price.  Tie-breaking favors lower latency and higher
// historical fill rate.

#include "routing/strategy.h"

namespace sor::routing
{

    class BestPriceStrategy final : public RoutingStrategy
    {
    public:
        RoutingDecision route(const Order &order,
                              const market_data::NBBO &nbbo,
                              const market_data::AggregatedBook &book,
                              const std::vector<VenueScore> &venues) override;

        const char *name() const noexcept override { return "BestPrice"; }
        sor::RoutingStrategy type() const noexcept override
        {
            return sor::RoutingStrategy::BestPrice;
        }

    private:
        // Compute a composite score for a venue at a given price.
        // Lower is better for buys; higher is better for sells.
        static double compute_venue_quality(const VenueScore &vs) noexcept;
    };

} // namespace sor::routing
