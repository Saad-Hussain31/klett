#include "routing/engine.h"
#include "market_data/aggregator.h"
#include "risk/risk_manager.h"
#include "infra/logging.h"

namespace sor::routing
{

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------
    RoutingEngine::RoutingEngine(market_data::MarketDataAggregator &md_aggregator,
                                 risk::RiskManager &risk_manager)
        : md_aggregator_(md_aggregator), risk_manager_(risk_manager) {}

    // ---------------------------------------------------------------------------
    // Strategy registration
    // ---------------------------------------------------------------------------
    void RoutingEngine::register_strategy(std::unique_ptr<RoutingStrategy> strategy)
    {
        if (!strategy)
        {
            return;
        }
        const auto tag = strategy->type();
        strategies_[tag] = std::move(strategy);
    }

    // ---------------------------------------------------------------------------
    // Venue management
    // ---------------------------------------------------------------------------
    void RoutingEngine::update_venue_score(VenueId venue_id, const VenueScore &score)
    {
        venue_scores_[venue_id] = score;
        venue_scores_[venue_id].venue_id = venue_id; // ensure consistency
    }

    void RoutingEngine::remove_venue(VenueId venue_id)
    {
        venue_scores_.erase(venue_id);
    }

    // ---------------------------------------------------------------------------
    // Callbacks
    // ---------------------------------------------------------------------------
    void RoutingEngine::set_order_callback(OrderCallback cb)
    {
        order_callback_ = std::move(cb);
    }

    void RoutingEngine::set_reject_callback(RejectCallback cb)
    {
        reject_callback_ = std::move(cb);
    }

    // ---------------------------------------------------------------------------
    // Statistics
    // ---------------------------------------------------------------------------
    RoutingEngine::Stats RoutingEngine::get_stats() const noexcept
    {
        return stats_;
    }

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------
    RoutingStrategy *RoutingEngine::get_strategy(sor::RoutingStrategy type) const
    {
        auto it = strategies_.find(type);
        if (it == strategies_.end())
        {
            return nullptr;
        }
        return it->second.get();
    }

    std::vector<VenueScore> RoutingEngine::get_available_venues() const
    {
        std::vector<VenueScore> result;
        result.reserve(venue_scores_.size());
        for (const auto &[id, score] : venue_scores_)
        {
            if (score.is_available)
            {
                result.push_back(score);
            }
        }
        return result;
    }

    // ---------------------------------------------------------------------------
    // route_order -- the main entry point
    //
    // 1. Basic order validation.
    // 2. Pre-trade risk check.
    // 3. Fetch market data (NBBO + aggregated book).
    // 4. Strategy dispatch.
    // 5. Update stats and fire callbacks.
    // ---------------------------------------------------------------------------
    RoutingDecision RoutingEngine::route_order(Order &order)
    {
        RoutingDecision empty_decision;

        // ---- Step 1: Basic validation ------------------------------------------

        if (order.symbol.empty())
        {
            ++stats_.orders_rejected;
            SOR_LOG_WARN("[RoutingEngine] Rejected order {}: empty symbol", order.id);
            if (reject_callback_)
            {
                reject_callback_(order, "empty symbol");
            }
            return empty_decision;
        }

        if (order.quantity <= 0)
        {
            ++stats_.orders_rejected;
            SOR_LOG_WARN("[RoutingEngine] Rejected order {}: invalid quantity {}", order.id, order.quantity);
            if (reject_callback_)
            {
                reject_callback_(order, "invalid quantity");
            }
            return empty_decision;
        }

        if (order.type == OrderType::Limit && order.price <= 0)
        {
            ++stats_.orders_rejected;
            SOR_LOG_WARN("[RoutingEngine] Rejected order {}: limit order with non-positive price", order.id);
            if (reject_callback_)
            {
                reject_callback_(order, "limit order with non-positive price");
            }
            return empty_decision;
        }

        // ---- Step 2: Risk check ------------------------------------------------

        const auto risk_result = risk_manager_.check_order(order);
        if (risk_result != risk::RiskCheckResult::Passed)
        {
            ++stats_.orders_rejected;
            SOR_LOG_WARN("[RoutingEngine] Order {} failed risk check: {}", order.id,
                         risk::RiskManager::to_string(risk_result));
            if (reject_callback_)
            {
                reject_callback_(order, risk::RiskManager::to_string(risk_result));
            }
            return empty_decision;
        }

        // ---- Step 3: Market data -----------------------------------------------

        const auto nbbo = md_aggregator_.get_nbbo(order.symbol);
        if (!nbbo.valid())
        {
            ++stats_.orders_rejected;
            if (reject_callback_)
            {
                reject_callback_(order, "no valid NBBO for symbol");
            }
            return empty_decision;
        }

        const auto book = md_aggregator_.get_aggregated_book(order.symbol);

        // ---- Step 4: Strategy dispatch -----------------------------------------

        auto *strategy = get_strategy(order.strategy);
        if (!strategy)
        {
            ++stats_.orders_rejected;
            if (reject_callback_)
            {
                reject_callback_(order, "no strategy registered for requested type");
            }
            return empty_decision;
        }

        const auto venues = get_available_venues();
        if (venues.empty())
        {
            ++stats_.orders_rejected;
            if (reject_callback_)
            {
                reject_callback_(order, "no available venues");
            }
            return empty_decision;
        }

        auto decision = strategy->route(order, nbbo, book, venues);

        // ---- Step 5: Post-routing bookkeeping ----------------------------------

        if (decision.valid())
        {
            ++stats_.orders_routed;
            stats_.total_slices += static_cast<uint64_t>(decision.slices.size());
            SOR_LOG_DEBUG("[RoutingEngine] Routed order {} -> {} slices",
                          order.id, decision.slices.size());

            if (order_callback_)
            {
                order_callback_(order);
            }
        }
        else
        {
            ++stats_.orders_rejected;
            SOR_LOG_WARN("[RoutingEngine] Order {} rejected: strategy returned empty decision", order.id);
            if (reject_callback_)
            {
                reject_callback_(order, "strategy returned empty decision");
            }
        }

        return decision;
    }

} // namespace sor::routing
