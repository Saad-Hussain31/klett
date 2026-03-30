// Unit tests for RiskManager.

#include <catch2/catch_test_macros.hpp>

#include "risk/risk_manager.h"
#include "core/types.h"
#include "core/order.h"

using namespace sor;
using namespace sor::risk;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Order make_buy_order(const char *sym, Quantity qty, double price)
{
    Order o{};
    o.id = 1;
    o.symbol = sym;
    o.side = Side::Buy;
    o.type = OrderType::Limit;
    o.tif = TimeInForce::GTC;
    o.price = to_price(price);
    o.quantity = qty;
    o.remaining_quantity = qty;
    o.filled_quantity = 0;
    o.state = OrderState::New;
    return o;
}

static RiskLimits permissive_limits()
{
    RiskLimits lim{};
    lim.enabled = true;
    lim.max_order_quantity = 10000;
    lim.max_order_notional = 1'000'000'000; // very permissive
    lim.max_position_quantity = 50000;
    lim.max_position_notional = 5'000'000'000;
    lim.max_orders_per_second = 10000;
    lim.max_open_orders = 1000;
    lim.max_loss = to_price(100'000.0);
    return lim;
}

// ============================================================================
// Basic passing
// ============================================================================

TEST_CASE("RiskManager: order passes all checks with permissive limits", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    auto order = make_buy_order("AAPL", 100, 150.0);
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
}

TEST_CASE("RiskManager: order passes with no limits configured (all zeros)", "[risk]")
{
    RiskManager rm;
    // Default limits are all zero -> not enforced.
    auto order = make_buy_order("AAPL", 100, 150.0);
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
}

// ============================================================================
// Max order quantity
// ============================================================================

TEST_CASE("RiskManager: fail max order quantity", "[risk]")
{
    RiskManager rm;
    RiskLimits lim = permissive_limits();
    lim.max_order_quantity = 50;
    rm.set_global_limits(lim);

    SECTION("Order within limit passes")
    {
        auto order = make_buy_order("AAPL", 50, 150.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    }

    SECTION("Order exceeding limit fails")
    {
        auto order = make_buy_order("AAPL", 51, 150.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::FailedMaxOrderQuantity);
    }
}

// ============================================================================
// Max order notional
// ============================================================================

TEST_CASE("RiskManager: fail max order notional", "[risk]")
{
    RiskManager rm;
    RiskLimits lim = permissive_limits();
    // Max notional $10,000.  The notional check computes
    // wide_mul_div(price, qty, PRICE_SCALE) which yields (price*qty)/PRICE_SCALE
    // i.e. an unscaled dollar integer.  So set the limit in the same units.
    lim.max_order_notional = 10'000;
    rm.set_global_limits(lim);

    SECTION("Under notional limit passes")
    {
        // 50 * $150 = $7,500 < $10,000
        auto order = make_buy_order("AAPL", 50, 150.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    }

    SECTION("Over notional limit fails")
    {
        // 100 * $150 = $15,000 > $10,000
        auto order = make_buy_order("AAPL", 100, 150.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::FailedMaxOrderNotional);
    }
}

// ============================================================================
// Max orders per second (rate limit)
// ============================================================================

TEST_CASE("RiskManager: fail max orders per second", "[risk]")
{
    RiskManager rm;
    RiskLimits lim = permissive_limits();
    lim.max_orders_per_second = 3;
    rm.set_global_limits(lim);

    auto order = make_buy_order("AAPL", 10, 150.0);

    // First 3 should pass.
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);

    // 4th should fail rate limit.
    REQUIRE(rm.check_order(order) == RiskCheckResult::FailedRateLimit);
}

// ============================================================================
// Max open orders
// ============================================================================

TEST_CASE("RiskManager: fail max open orders", "[risk]")
{
    RiskManager rm;
    RiskLimits lim = permissive_limits();
    lim.max_open_orders = 2;
    rm.set_global_limits(lim);

    auto o1 = make_buy_order("AAPL", 10, 150.0);
    o1.id = 1;
    auto o2 = make_buy_order("AAPL", 10, 150.0);
    o2.id = 2;

    // Simulate acceptance to increment open orders.
    REQUIRE(rm.check_order(o1) == RiskCheckResult::Passed);
    rm.on_order_accepted(o1);

    REQUIRE(rm.check_order(o2) == RiskCheckResult::Passed);
    rm.on_order_accepted(o2);

    // Third order should fail.
    auto o3 = make_buy_order("AAPL", 10, 150.0);
    o3.id = 3;
    REQUIRE(rm.check_order(o3) == RiskCheckResult::FailedMaxOpenOrders);
}

// ============================================================================
// Kill switch
// ============================================================================

TEST_CASE("RiskManager: kill switch blocks all orders", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    auto order = make_buy_order("AAPL", 10, 150.0);
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);

    rm.activate_kill_switch();
    REQUIRE(rm.is_kill_switch_active());
    REQUIRE(rm.check_order(order) == RiskCheckResult::FailedKillSwitch);
}

TEST_CASE("RiskManager: kill switch deactivate re-enables trading", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    rm.activate_kill_switch();
    REQUIRE(rm.is_kill_switch_active());

    auto order = make_buy_order("AAPL", 10, 150.0);
    REQUIRE(rm.check_order(order) == RiskCheckResult::FailedKillSwitch);

    rm.deactivate_kill_switch();
    REQUIRE_FALSE(rm.is_kill_switch_active());
    REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
}

// ============================================================================
// Symbol-specific limits
// ============================================================================

TEST_CASE("RiskManager: symbol-specific limits override", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    // Set a very tight limit just for TSLA.
    RiskLimits tsla_lim{};
    tsla_lim.enabled = true;
    tsla_lim.max_order_quantity = 5;
    rm.set_symbol_limits(Symbol("TSLA"), tsla_lim);

    SECTION("TSLA order exceeding symbol limit fails")
    {
        auto order = make_buy_order("TSLA", 10, 200.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::FailedMaxOrderQuantity);
    }

    SECTION("TSLA order within symbol limit passes")
    {
        auto order = make_buy_order("TSLA", 5, 200.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    }

    SECTION("AAPL unaffected by TSLA limits")
    {
        auto order = make_buy_order("AAPL", 1000, 150.0);
        REQUIRE(rm.check_order(order) == RiskCheckResult::Passed);
    }
}

// ============================================================================
// Position tracking after fills
// ============================================================================

TEST_CASE("RiskManager: position tracking after fills", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    Symbol sym("AAPL");

    SECTION("Buy fill increases net quantity")
    {
        rm.on_fill(sym, Side::Buy, 100, to_price(150.0));
        auto pos = rm.get_position(sym);
        REQUIRE(pos.net_quantity == 100);
    }

    SECTION("Sell fill decreases net quantity")
    {
        rm.on_fill(sym, Side::Buy, 100, to_price(150.0));
        rm.on_fill(sym, Side::Sell, 50, to_price(155.0));
        auto pos = rm.get_position(sym);
        REQUIRE(pos.net_quantity == 50);
    }

    SECTION("Fill updates average entry price")
    {
        rm.on_fill(sym, Side::Buy, 100, to_price(150.0));
        auto pos = rm.get_position(sym);
        REQUIRE(pos.avg_entry_price == to_price(150.0));
    }

    SECTION("Closing fill generates realized PnL")
    {
        rm.on_fill(sym, Side::Buy, 100, to_price(100.0));
        rm.on_fill(sym, Side::Sell, 100, to_price(110.0));
        auto pos = rm.get_position(sym);
        // PnL per unit = $10, 100 units = $1000
        // But PnL uses fixed-point: (110-100) * 100 / PRICE_SCALE
        REQUIRE(pos.realized_pnl > 0);
        REQUIRE(pos.net_quantity == 0);
    }
}

TEST_CASE("RiskManager: on_order_accepted increments open count", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    auto order = make_buy_order("AAPL", 100, 150.0);
    rm.on_order_accepted(order);

    auto pos = rm.get_position(Symbol("AAPL"));
    REQUIRE(pos.open_order_count == 1);
    REQUIRE(pos.pending_buy_quantity == 100);
}

TEST_CASE("RiskManager: on_order_canceled decrements open count", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    auto order = make_buy_order("AAPL", 100, 150.0);
    rm.on_order_accepted(order);

    auto pos = rm.get_position(Symbol("AAPL"));
    REQUIRE(pos.open_order_count == 1);

    rm.on_order_canceled(order);
    pos = rm.get_position(Symbol("AAPL"));
    REQUIRE(pos.open_order_count == 0);
    REQUIRE(pos.pending_buy_quantity == 0);
}

TEST_CASE("RiskManager: on_order_rejected decrements tracking", "[risk]")
{
    RiskManager rm;
    rm.set_global_limits(permissive_limits());

    auto order = make_buy_order("AAPL", 100, 150.0);
    rm.on_order_accepted(order);
    rm.on_order_rejected(order);

    auto pos = rm.get_position(Symbol("AAPL"));
    CHECK(pos.open_order_count == 0);
    CHECK(pos.pending_buy_quantity == 0);
}

// ============================================================================
// to_string
// ============================================================================

TEST_CASE("RiskManager: to_string for results", "[risk]")
{
    CHECK(std::string(RiskManager::to_string(RiskCheckResult::Passed)) == "Passed");
    CHECK(std::string(RiskManager::to_string(RiskCheckResult::FailedKillSwitch)) == "FailedKillSwitch");
    CHECK(std::string(RiskManager::to_string(RiskCheckResult::FailedMaxOrderQuantity)) == "FailedMaxOrderQuantity");
}
