#include "risk/risk_manager.h"
#include <cmath>
#include <algorithm>

namespace sor::risk {

// ---------------------------------------------------------------------------
// Overflow-safe wide multiply: (a * b) / divisor.
// Uses compiler __int128 extension (universally available on x86-64 Linux
// with GCC and Clang).  Wrapped in a helper to localise the pragma.
// ---------------------------------------------------------------------------
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static inline Price wide_mul_div(int64_t a, int64_t b, int64_t divisor) noexcept
{
    return static_cast<Price>(
        static_cast<__int128>(a) * static_cast<__int128>(b) /
        static_cast<__int128>(divisor));
}
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------------
// Default constructor -- sensible defaults that pass everything until
// limits are explicitly configured.
// ---------------------------------------------------------------------------
RiskManager::RiskManager()
    : rate_limiter_(10000)  // 10 000 orders/sec default ceiling
{
}

// ---------------------------------------------------------------------------
// Pre-trade risk check.
//
// Check order is intentionally sequenced from cheapest (atomic loads) to
// most expensive (map lookups under mutex) so that the common "pass" path
// incurs minimal overhead.
// ---------------------------------------------------------------------------
RiskCheckResult RiskManager::check_order(const Order& order)
{
    // 1. Kill switch -- single atomic load, no lock.
    if (kill_switch_.is_active()) [[unlikely]]
        return RiskCheckResult::FailedKillSwitch;

    // 2. Rate limit -- atomic CAS, no lock.
    if (global_limits_.max_orders_per_second > 0) {
        if (!rate_limiter_.try_acquire())
            return RiskCheckResult::FailedRateLimit;
    }

    // 3 - 8. Limit checks require position info (under lock).
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& pos = positions_[order.symbol];  // default-constructed if new

    // Check against global limits.
    auto result = check_limits(order, global_limits_, pos);
    if (result != RiskCheckResult::Passed)
        return result;

    // 9. Check symbol-specific limits if configured.
    auto it = symbol_limits_.find(order.symbol);
    if (it != symbol_limits_.end()) {
        result = check_limits(order, it->second, pos);
        if (result != RiskCheckResult::Passed)
            return result;
    }

    return RiskCheckResult::Passed;
}

// ---------------------------------------------------------------------------
// check_limits -- evaluate a single RiskLimits struct against the order.
// ---------------------------------------------------------------------------
RiskCheckResult RiskManager::check_limits(
    const Order& order,
    const RiskLimits& limits,
    const PositionInfo& pos) const
{
    if (!limits.enabled)
        return RiskCheckResult::Passed;

    // 3. Max order quantity.
    if (limits.max_order_quantity > 0 &&
        order.quantity > limits.max_order_quantity) {
        return RiskCheckResult::FailedMaxOrderQuantity;
    }

    // 4. Max order notional (price * quantity, both in fixed-point).
    //    For market orders price may be INVALID_PRICE; skip the check.
    if (limits.max_order_notional > 0 &&
        order.price != INVALID_PRICE) {
        const Price notional = wide_mul_div(order.price, order.quantity, PRICE_SCALE);
        if (notional > limits.max_order_notional) {
            return RiskCheckResult::FailedMaxOrderNotional;
        }
    }

    // 5. Max position quantity (|current + pending + new order|).
    if (limits.max_position_quantity > 0) {
        const Quantity pending = (order.side == Side::Buy)
            ? pos.pending_buy_quantity
            : pos.pending_sell_quantity;
        const Quantity order_delta = (order.side == Side::Buy)
            ? order.quantity
            : -order.quantity;
        const Quantity projected = pos.net_quantity + order_delta +
            ((order.side == Side::Buy) ? pending : -pending);
        if (std::abs(projected) > limits.max_position_quantity) {
            return RiskCheckResult::FailedMaxPositionQuantity;
        }
    }

    // 6. Max position notional.
    if (limits.max_position_notional > 0 &&
        order.price != INVALID_PRICE) {
        const Quantity order_delta = (order.side == Side::Buy)
            ? order.quantity
            : -order.quantity;
        const Quantity projected_qty = pos.net_quantity + order_delta;
        const Price projected_notional =
            wide_mul_div(std::abs(projected_qty), order.price, PRICE_SCALE);
        if (projected_notional > limits.max_position_notional) {
            return RiskCheckResult::FailedMaxPositionNotional;
        }
    }

    // 7. Max open orders.
    if (limits.max_open_orders > 0 &&
        pos.open_order_count >= limits.max_open_orders) {
        return RiskCheckResult::FailedMaxOpenOrders;
    }

    // 8. Max loss (realised + unrealised PnL exceeds threshold).
    //    max_loss is stored as a positive value; actual loss is negative PnL.
    if (limits.max_loss > 0) {
        const Price total_pnl = pos.realized_pnl + pos.unrealized_pnl;
        if (total_pnl < 0 && (-total_pnl) > limits.max_loss) {
            return RiskCheckResult::FailedMaxLoss;
        }
    }

    return RiskCheckResult::Passed;
}

// ---------------------------------------------------------------------------
// Position updates.
// ---------------------------------------------------------------------------

void RiskManager::on_fill(const Symbol& symbol, Side side,
                          Quantity qty, Price price)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[symbol];

    const Quantity signed_qty = (side == Side::Buy) ? qty : -qty;

    // Update realised PnL when reducing a position.
    const bool reducing =
        (pos.net_quantity > 0 && side == Side::Sell) ||
        (pos.net_quantity < 0 && side == Side::Buy);

    if (reducing && pos.avg_entry_price != 0) {
        const Quantity close_qty = std::min(std::abs(signed_qty),
                                            std::abs(pos.net_quantity));
        // PnL per unit = (fill_price - avg_entry) * direction
        const Price pnl_per_unit = (pos.net_quantity > 0)
            ? (price - pos.avg_entry_price)
            : (pos.avg_entry_price - price);
        pos.realized_pnl += wide_mul_div(pnl_per_unit, close_qty, PRICE_SCALE);
    }

    // Update average entry price (FIFO-style simplification).
    if (!reducing || std::abs(signed_qty) > std::abs(pos.net_quantity)) {
        // Opening or flipping: compute new weighted average.
        const Quantity new_open_qty =
            std::abs(pos.net_quantity + signed_qty);
        if (new_open_qty > 0) {
            if (std::abs(pos.net_quantity) > 0 && !reducing) {
                // Adding to existing position: weighted average.
                const Quantity existing_abs = std::abs(pos.net_quantity);
                pos.avg_entry_price = wide_mul_div(
                    pos.avg_entry_price, existing_abs, existing_abs + qty) +
                    wide_mul_div(price, qty, existing_abs + qty);
            } else {
                // New position or flip: avg is the fill price.
                pos.avg_entry_price = price;
            }
        } else {
            pos.avg_entry_price = 0;
        }
    }

    pos.net_quantity += signed_qty;

    // Decrement pending quantities.
    if (side == Side::Buy) {
        pos.pending_buy_quantity =
            std::max<Quantity>(0, pos.pending_buy_quantity - qty);
    } else {
        pos.pending_sell_quantity =
            std::max<Quantity>(0, pos.pending_sell_quantity - qty);
    }
}

void RiskManager::on_order_accepted(const Order& order)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[order.symbol];
    pos.open_order_count++;
    if (order.side == Side::Buy) {
        pos.pending_buy_quantity += order.quantity;
    } else {
        pos.pending_sell_quantity += order.quantity;
    }
}

void RiskManager::on_order_canceled(const Order& order)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[order.symbol];
    pos.open_order_count = std::max(0, pos.open_order_count - 1);
    const Quantity remaining = order.quantity - order.filled_quantity;
    if (order.side == Side::Buy) {
        pos.pending_buy_quantity =
            std::max<Quantity>(0, pos.pending_buy_quantity - remaining);
    } else {
        pos.pending_sell_quantity =
            std::max<Quantity>(0, pos.pending_sell_quantity - remaining);
    }
}

void RiskManager::on_order_rejected(const Order& order)
{
    // Rejected orders never reached the venue, so we only need to
    // decrement the open order count if it was previously incremented
    // (i.e., the order had been accepted before a cancel-reject).
    std::lock_guard<std::mutex> lock(mutex_);
    auto& pos = positions_[order.symbol];
    pos.open_order_count = std::max(0, pos.open_order_count - 1);
    if (order.side == Side::Buy) {
        pos.pending_buy_quantity =
            std::max<Quantity>(0, pos.pending_buy_quantity - order.quantity);
    } else {
        pos.pending_sell_quantity =
            std::max<Quantity>(0, pos.pending_sell_quantity - order.quantity);
    }
}

// ---------------------------------------------------------------------------
// Limit configuration.
// ---------------------------------------------------------------------------

void RiskManager::set_global_limits(const RiskLimits& limits)
{
    std::lock_guard<std::mutex> lock(mutex_);
    global_limits_ = limits;
    // Reconfigure rate limiter if the per-second cap changed.
    if (limits.max_orders_per_second > 0) {
        rate_limiter_.set_max_rate(limits.max_orders_per_second);
    }
}

void RiskManager::set_symbol_limits(const Symbol& symbol,
                                    const RiskLimits& limits)
{
    std::lock_guard<std::mutex> lock(mutex_);
    symbol_limits_[symbol] = limits;
}

// ---------------------------------------------------------------------------
// Position queries.
// ---------------------------------------------------------------------------

PositionInfo RiskManager::get_position(const Symbol& symbol) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = positions_.find(symbol);
    if (it != positions_.end())
        return it->second;
    return {};
}

// ---------------------------------------------------------------------------
// Kill switch delegation.
// ---------------------------------------------------------------------------

void RiskManager::activate_kill_switch()
{
    kill_switch_.activate("Manual kill switch activation");
}

void RiskManager::deactivate_kill_switch()
{
    kill_switch_.deactivate();
}

bool RiskManager::is_kill_switch_active() const
{
    return kill_switch_.is_active();
}

// ---------------------------------------------------------------------------
// Rate-limit check.
// ---------------------------------------------------------------------------

bool RiskManager::check_rate_limit()
{
    return rate_limiter_.try_acquire();
}

// ---------------------------------------------------------------------------
// String conversion.
// ---------------------------------------------------------------------------

const char* RiskManager::to_string(RiskCheckResult result) noexcept
{
    switch (result) {
    case RiskCheckResult::Passed:                   return "Passed";
    case RiskCheckResult::FailedMaxOrderQuantity:   return "FailedMaxOrderQuantity";
    case RiskCheckResult::FailedMaxOrderNotional:   return "FailedMaxOrderNotional";
    case RiskCheckResult::FailedMaxPositionNotional: return "FailedMaxPositionNotional";
    case RiskCheckResult::FailedMaxPositionQuantity: return "FailedMaxPositionQuantity";
    case RiskCheckResult::FailedMaxOrdersPerSecond: return "FailedMaxOrdersPerSecond";
    case RiskCheckResult::FailedMaxOpenOrders:      return "FailedMaxOpenOrders";
    case RiskCheckResult::FailedMaxLoss:            return "FailedMaxLoss";
    case RiskCheckResult::FailedKillSwitch:         return "FailedKillSwitch";
    case RiskCheckResult::FailedRateLimit:          return "FailedRateLimit";
    case RiskCheckResult::FailedSymbolNotAllowed:   return "FailedSymbolNotAllowed";
    }
    return "Unknown";
}

} // namespace sor::risk
