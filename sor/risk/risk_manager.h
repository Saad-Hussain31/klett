#pragma once

// Pre-trade risk management.
//
// Every order passes through check_order() before being routed to a venue.
// The checks are ordered from cheapest to most expensive:
//   1. Kill switch   (atomic load)
//   2. Rate limit    (atomic CAS)
//   3. Per-order limits (pure arithmetic)
//   4. Position limits  (map lookup + arithmetic)
//   5. Symbol-specific overrides
//
// Position tracking is updated on fills and order lifecycle events.

#include "core/types.h"
#include "core/order.h"
#include "risk/rate_limiter.h"
#include "risk/kill_switch.h"
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace sor::risk
{

    // ---------------------------------------------------------------------------
    // Configuration: per-symbol or global risk limits.
    // A zero value for any limit means "not enforced".
    // ---------------------------------------------------------------------------
    struct RiskLimits
    {
        Quantity max_order_quantity{0};
        Price max_order_notional{0}; // price * qty in fixed-point
        Price max_position_notional{0};
        Quantity max_position_quantity{0};
        int32_t max_orders_per_second{0};
        int32_t max_open_orders{0};
        Price max_loss{0}; // max unrealised + realised loss (positive value)
        bool enabled{true};
    };

    // ---------------------------------------------------------------------------
    // Live position snapshot for a single symbol.
    // ---------------------------------------------------------------------------
    struct PositionInfo
    {
        Quantity net_quantity{0}; // positive = long, negative = short
        Price avg_entry_price{0};
        Price realized_pnl{0};
        Price unrealized_pnl{0};
        int32_t open_order_count{0};
        Quantity pending_buy_quantity{0};
        Quantity pending_sell_quantity{0};
    };

    // ---------------------------------------------------------------------------
    // Risk check verdict -- the first failing check is returned.
    // ---------------------------------------------------------------------------
    enum class RiskCheckResult : uint8_t
    {
        Passed,
        FailedMaxOrderQuantity,
        FailedMaxOrderNotional,
        FailedMaxPositionNotional,
        FailedMaxPositionQuantity,
        FailedMaxOrdersPerSecond,
        FailedMaxOpenOrders,
        FailedMaxLoss,
        FailedKillSwitch,
        FailedRateLimit,
        FailedSymbolNotAllowed,
    };

    // ---------------------------------------------------------------------------
    // RiskManager
    // ---------------------------------------------------------------------------
    class RiskManager
    {
    public:
        RiskManager();

        // -----------------------------------------------------------------------
        // Pre-trade check.  Returns Passed or the first failing reason.
        // -----------------------------------------------------------------------
        [[nodiscard]] RiskCheckResult check_order(const Order &order);

        // -----------------------------------------------------------------------
        // Position / order-event updates.
        // -----------------------------------------------------------------------
        void on_fill(const Symbol &symbol, Side side, Quantity qty, Price price);
        void on_order_accepted(const Order &order);
        void on_order_canceled(const Order &order);
        void on_order_rejected(const Order &order);

        // -----------------------------------------------------------------------
        // Limit configuration.
        // -----------------------------------------------------------------------
        void set_global_limits(const RiskLimits &limits);
        void set_symbol_limits(const Symbol &symbol, const RiskLimits &limits);

        // -----------------------------------------------------------------------
        // Position queries.
        // -----------------------------------------------------------------------
        [[nodiscard]] PositionInfo get_position(const Symbol &symbol) const;

        // -----------------------------------------------------------------------
        // Kill switch pass-through.
        // -----------------------------------------------------------------------
        void activate_kill_switch();
        void deactivate_kill_switch();
        [[nodiscard]] bool is_kill_switch_active() const;

        // -----------------------------------------------------------------------
        // Rate-limit check (exposed for direct use if needed).
        // -----------------------------------------------------------------------
        [[nodiscard]] bool check_rate_limit();

        [[nodiscard]] static const char *to_string(RiskCheckResult result) noexcept;

    private:
        // Check an order against a specific set of limits (global or per-symbol).
        [[nodiscard]] RiskCheckResult check_limits(
            const Order &order,
            const RiskLimits &limits,
            const PositionInfo &pos) const;

        RiskLimits global_limits_;
        std::unordered_map<Symbol, RiskLimits> symbol_limits_;
        mutable std::unordered_map<Symbol, PositionInfo> positions_;

        KillSwitch kill_switch_;
        RateLimiter rate_limiter_;

        mutable std::mutex mutex_; // protects positions_ and symbol_limits_
    };

} // namespace sor::risk
