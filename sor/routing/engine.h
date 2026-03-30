#pragma once

// RoutingEngine -- the main orchestrator for the Smart Order Router.
//
// Receives parent orders, performs validation and risk checks, looks up
// the appropriate strategy, and produces a RoutingDecision that the
// execution layer converts into child orders dispatched to venues.

#include "core/types.h"
#include "core/order.h"
#include "routing/strategy.h"
#include <memory>
#include <unordered_map>
#include <functional>

// Forward declarations -- avoid including heavy headers in this header.
namespace sor::market_data
{
    class MarketDataAggregator;
} // namespace sor::market_data

namespace sor::risk
{
    class RiskManager;
    enum class RiskCheckResult : uint8_t;
} // namespace sor::risk

namespace sor::routing
{

    class RoutingEngine
    {
    public:
        // -- Callback types -------------------------------------------------------

        /// Called after a successful routing decision (one call per parent order).
        using OrderCallback = std::function<void(Order &)>;

        /// Called when an order is rejected (validation or risk failure).
        using RejectCallback = std::function<void(const Order &, const char *reason)>;

        // -- Construction ---------------------------------------------------------

        RoutingEngine(market_data::MarketDataAggregator &md_aggregator,
                      risk::RiskManager &risk_manager);

        // Non-copyable, non-movable (holds references to external systems).
        RoutingEngine(const RoutingEngine &) = delete;
        RoutingEngine &operator=(const RoutingEngine &) = delete;
        RoutingEngine(RoutingEngine &&) = delete;
        RoutingEngine &operator=(RoutingEngine &&) = delete;

        ~RoutingEngine() = default;

        // -- Strategy registration ------------------------------------------------

        /// Register a routing strategy.  Replaces any previous strategy with the
        /// same RoutingStrategy enum tag.
        void register_strategy(std::unique_ptr<RoutingStrategy> strategy);

        // -- Order routing --------------------------------------------------------

        /// Main entry point.  Validates the order, runs risk checks, queries
        /// market data, delegates to the appropriate strategy, and returns the
        /// routing decision.  An empty (invalid) decision signals rejection.
        RoutingDecision route_order(Order &order);

        // -- Venue management -----------------------------------------------------

        /// Update quality metrics for a venue.
        void update_venue_score(VenueId venue_id, const VenueScore &score);

        /// Remove a venue from the available pool.
        void remove_venue(VenueId venue_id);

        // -- Callbacks ------------------------------------------------------------

        void set_order_callback(OrderCallback cb);
        void set_reject_callback(RejectCallback cb);

        // -- Statistics -----------------------------------------------------------

        struct Stats
        {
            uint64_t orders_routed{0};
            uint64_t orders_rejected{0};
            uint64_t total_slices{0};
        };

        [[nodiscard]] Stats get_stats() const noexcept;

    private:
        // Lookup the registered strategy for a given enum tag.
        [[nodiscard]] RoutingStrategy *get_strategy(sor::RoutingStrategy type) const;

        // Collect available venue scores as a flat vector for strategy consumption.
        [[nodiscard]] std::vector<VenueScore> get_available_venues() const;

        // -- Dependencies (injected) ---------------------------------------------
        market_data::MarketDataAggregator &md_aggregator_;
        risk::RiskManager &risk_manager_;

        // -- Internal state -------------------------------------------------------
        std::unordered_map<sor::RoutingStrategy, std::unique_ptr<RoutingStrategy>> strategies_;
        std::unordered_map<VenueId, VenueScore> venue_scores_;

        OrderCallback order_callback_;
        RejectCallback reject_callback_;

        Stats stats_;
    };

} // namespace sor::routing
