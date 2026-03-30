// Integration test: partial fills -- verify state transitions and cumulative fills.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "connectors/simulated_exchange.h"
#include "execution/execution_handler.h"
#include "state/order_state_machine.h"
#include "core/types.h"
#include "core/order.h"

#include <vector>
#include <chrono>

using namespace sor;
using namespace sor::connectors;
using namespace sor::execution;
using namespace sor::state;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create an exchange that always does partial fills.
static SimulatedExchange make_partial_fill_exchange(VenueId id = 1)
{
    SimulatedExchange::Config cfg;
    cfg.venue_id = id;
    cfg.name = "PartialExchange";
    cfg.latency = std::chrono::microseconds(0);
    cfg.latency_jitter = std::chrono::microseconds(0);
    cfg.fill_probability = 1.0;
    cfg.partial_fill_probability = 1.0; // always partial
    cfg.reject_probability = 0.0;
    cfg.fee_rate = 0.001;
    return SimulatedExchange(cfg);
}

/// Create a deterministic, full-fill exchange.
static SimulatedExchange make_full_fill_exchange(VenueId id = 1)
{
    SimulatedExchange::Config cfg;
    cfg.venue_id = id;
    cfg.name = "FullExchange";
    cfg.latency = std::chrono::microseconds(0);
    cfg.latency_jitter = std::chrono::microseconds(0);
    cfg.fill_probability = 1.0;
    cfg.partial_fill_probability = 0.0;
    cfg.reject_probability = 0.0;
    cfg.fee_rate = 0.001;
    return SimulatedExchange(cfg);
}

static Order make_buy(OrderId id, Quantity qty, double price)
{
    Order o{};
    o.id = id;
    o.client_order_id = id;
    o.symbol = "AAPL";
    o.side = Side::Buy;
    o.type = OrderType::Limit;
    o.tif = TimeInForce::GTC;
    o.price = to_price(price);
    o.quantity = qty;
    o.remaining_quantity = qty;
    o.filled_quantity = 0;
    o.state = OrderState::New;
    o.create_time = std::chrono::steady_clock::now();
    return o;
}

// ============================================================================
// Partial fill followed by full fill
// ============================================================================

TEST_CASE("PartialFills: receive partial then remaining fill",
          "[integration][partial_fills]")
{
    auto exchange = make_partial_fill_exchange(1);
    ExecutionHandler handler;

    std::vector<ExecutionReport> reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(1, 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // First matching cycle -- should produce partial fill.
    exchange.process_matching();

    // Find the partial fill report.
    bool found_partial = false;
    for (const auto &r : reports)
    {
        if (r.state == OrderState::PartiallyFilled)
        {
            found_partial = true;
            CHECK(r.last_quantity > 0);
            CHECK(r.last_quantity < 100);
            CHECK(r.cum_quantity < 100);
            CHECK(r.leaves_quantity > 0);
        }
    }
    REQUIRE(found_partial);

    // Check order is in PartiallyFilled state.
    const Order *tracked = handler.get_order(1);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::PartiallyFilled);
    CHECK(tracked->filled_quantity > 0);
    CHECK(tracked->filled_quantity < 100);

    // Continue matching until fully filled (may take several cycles because
    // partial_fill_probability is 1.0, but each partial moves forward).
    int max_cycles = 50;
    while (max_cycles-- > 0)
    {
        exchange.process_matching();
        tracked = handler.get_order(1);
        if (tracked->state == OrderState::Filled)
        {
            break;
        }
    }

    tracked = handler.get_order(1);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Filled);
    CHECK(tracked->filled_quantity == 100);
    CHECK(tracked->remaining_quantity == 0);
}

// ============================================================================
// Cumulative fill quantities and average price
// ============================================================================

TEST_CASE("PartialFills: cumulative fill quantities are consistent",
          "[integration][partial_fills]")
{
    auto exchange = make_partial_fill_exchange(1);
    ExecutionHandler handler;

    std::vector<ExecutionReport> all_reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        all_reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(2, 200, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // Run matching until filled.
    int max_cycles = 100;
    while (max_cycles-- > 0)
    {
        exchange.process_matching();
        const Order *tracked = handler.get_order(2);
        if (tracked && tracked->state == OrderState::Filled)
        {
            break;
        }
    }

    // Verify cumulative quantities in reports are monotonically increasing.
    Quantity prev_cum = 0;
    for (const auto &r : all_reports)
    {
        if (r.order_id != 2)
            continue;
        if (r.last_quantity > 0)
        {
            CHECK(r.cum_quantity > prev_cum);
            prev_cum = r.cum_quantity;
        }
    }

    // Final state.
    const Order *tracked = handler.get_order(2);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Filled);
    CHECK(tracked->filled_quantity == 200);
    CHECK(tracked->remaining_quantity == 0);
    // Average fill price should be valid.
    CHECK(tracked->avg_fill_price > 0);
}

// ============================================================================
// Average fill price calculation
// ============================================================================

TEST_CASE("PartialFills: average fill price computed correctly on uniform fills",
          "[integration][partial_fills]")
{
    // Use a full-fill exchange for predictable price.
    auto exchange = make_full_fill_exchange(1);
    ExecutionHandler handler;

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(3, 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    exchange.process_matching();

    const Order *tracked = handler.get_order(3);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Filled);
    // All filled at market ask = 150.0.
    CHECK(tracked->avg_fill_price == to_price(150.0));
}

// ============================================================================
// Parent-child partial fills
// ============================================================================

TEST_CASE("PartialFills: child partial fills propagate to parent",
          "[integration][partial_fills]")
{
    auto exchange = make_full_fill_exchange(1);
    ExecutionHandler handler;

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    // Create parent order (not sent directly).
    Order parent{};
    parent.id = 100;
    parent.client_order_id = 100;
    parent.symbol = "AAPL";
    parent.side = Side::Buy;
    parent.type = OrderType::Limit;
    parent.tif = TimeInForce::GTC;
    parent.price = to_price(151.0);
    parent.quantity = 200;
    parent.remaining_quantity = 200;
    parent.state = OrderState::Accepted;
    parent.create_time = std::chrono::steady_clock::now();

    handler.track_order(parent);

    // Create two child orders, each for half the parent.
    Order child1{};
    child1.id = 201;
    child1.client_order_id = 201;
    child1.parent_order_id = 100;
    child1.symbol = parent.symbol;
    child1.side = parent.side;
    child1.type = parent.type;
    child1.tif = parent.tif;
    child1.price = parent.price;
    child1.quantity = 100;
    child1.remaining_quantity = 100;
    child1.state = OrderState::PendingNew;
    child1.create_time = std::chrono::steady_clock::now();

    Order child2 = child1;
    child2.id = 202;
    child2.client_order_id = 202;

    handler.track_child_order(100, child1);
    handler.track_child_order(100, child2);

    // Send child1, fill it.
    REQUIRE(exchange.send_order(child1));
    exchange.process_matching();

    // Parent should be PartiallyFilled after first child fills.
    const Order *tracked_parent = handler.get_order(100);
    REQUIRE(tracked_parent != nullptr);
    CHECK(tracked_parent->filled_quantity == 100);
    CHECK(tracked_parent->remaining_quantity == 100);

    // Send and fill child2.
    REQUIRE(exchange.send_order(child2));
    exchange.process_matching();

    // Parent should now be Filled.
    tracked_parent = handler.get_order(100);
    REQUIRE(tracked_parent != nullptr);
    CHECK(tracked_parent->state == OrderState::Filled);
    CHECK(tracked_parent->filled_quantity == 200);
    CHECK(tracked_parent->remaining_quantity == 0);
    CHECK(tracked_parent->avg_fill_price == to_price(150.0));
}

// ============================================================================
// Fill callback fires for each partial fill
// ============================================================================

TEST_CASE("PartialFills: fill callback fires for each fill event",
          "[integration][partial_fills]")
{
    auto exchange = make_partial_fill_exchange(1);
    ExecutionHandler handler;

    int fill_callback_count = 0;
    handler.set_fill_callback([&](const Order &, const ExecutionReport &rpt)
                              {
        ++fill_callback_count;
        CHECK(rpt.last_quantity > 0); });

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(4, 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // Run enough cycles to fill.
    int max_cycles = 50;
    while (max_cycles-- > 0)
    {
        exchange.process_matching();
        const Order *tracked = handler.get_order(4);
        if (tracked && tracked->state == OrderState::Filled)
        {
            break;
        }
    }

    // Should have received at least 2 fill callbacks (partial + final).
    CHECK(fill_callback_count >= 2);
}

// ============================================================================
// Handler stats
// ============================================================================

TEST_CASE("PartialFills: handler stats track partial and total fills",
          "[integration][partial_fills]")
{
    auto exchange = make_partial_fill_exchange(1);
    ExecutionHandler handler;

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(5, 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    int max_cycles = 50;
    while (max_cycles-- > 0)
    {
        exchange.process_matching();
        const Order *tracked = handler.get_order(5);
        if (tracked && tracked->state == OrderState::Filled)
        {
            break;
        }
    }

    auto stats = handler.get_stats();
    CHECK(stats.total_fills >= 1);
    CHECK(stats.total_partial_fills >= 1);
}
