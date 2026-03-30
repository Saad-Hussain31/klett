#pragma once

// Base routing strategy interface and common types.
//
// Every routing strategy takes an order, current market state, and venue
// scores, then produces a RoutingDecision -- a set of child-order slices
// that the engine will dispatch to venues.

#include "core/types.h"
#include "core/order.h"
#include <vector>

// Forward declarations to avoid pulling in heavy headers.
namespace sor::market_data
{
    struct NBBO;
    struct AggregatedBook;
} // namespace sor::market_data

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // RoutingDecision -- the output of every strategy.
    // ---------------------------------------------------------------------------

    struct RoutingDecision
    {
        struct Slice
        {
            VenueId venue_id;
            Price price;
            Quantity quantity;
            OrderType type{OrderType::Limit};
            TimeInForce tif{TimeInForce::IOC};
        };

        std::vector<Slice> slices;

        [[nodiscard]] bool valid() const noexcept { return !slices.empty(); }

        [[nodiscard]] Quantity total_quantity() const noexcept
        {
            Quantity total{0};
            for (const auto &s : slices)
            {
                total += s.quantity;
            }
            return total;
        }
    };

    // ---------------------------------------------------------------------------
    // VenueScore -- per-venue quality metrics consumed by strategies.
    // ---------------------------------------------------------------------------

    struct VenueScore
    {
        VenueId venue_id{0};
        double latency_us{0.0};      // average round-trip latency in microseconds
        double fill_rate{0.0};       // historical fill rate [0, 1]
        double fee_rate{0.0};        // fee as a fraction (e.g. 0.001 = 10 bps)
        Price fee_adjusted_price{0}; // price after fee adjustment
        bool is_available{true};
    };

    // ---------------------------------------------------------------------------
    // RoutingStrategy -- abstract base class for all strategies.
    // ---------------------------------------------------------------------------

    class RoutingStrategy
    {
    public:
        virtual ~RoutingStrategy() = default;

        // Produce a routing decision for the given order and market context.
        virtual RoutingDecision route(const Order &order,
                                      const market_data::NBBO &nbbo,
                                      const market_data::AggregatedBook &book,
                                      const std::vector<VenueScore> &venues) = 0;

        // Human-readable name for logging.
        virtual const char *name() const noexcept = 0;

        // Enum tag so the engine can dispatch by RoutingStrategy enum.
        virtual sor::RoutingStrategy type() const noexcept = 0;
    };

} // namespace sor::routing
