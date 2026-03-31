// Integration test: partial fills -- verify state transitions and cumulative fills.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "connectors/simulated_exchange.h"
#include "execution/execution_handler.h"
#include "state/order_state_machine.h"
#include "core/types.h"
#include "core/order.h"
#include "test_helpers.h"

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
        FillLogger::log_exec_report(rpt, "exec callback");
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));
    std::cout << "[TEST] Partial fill exchange connected, market: bid=149, ask=150\n";

    auto order = make_buy(1, 100, 151.0);
    std::cout << "[TEST] Created buy order: id=1, qty=100, px=151.0\n";
    StateTransitionLogger::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // First matching cycle -- should produce partial fill.
    exchange.process_matching();
    std::cout << "[FILL] After first matching cycle, reports: " << reports.size() << "\n";

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
    CHECK_WITH_LOG(tracked->state == OrderState::PartiallyFilled, "order partially filled");
    CHECK(tracked->filled_quantity > 0);
    CHECK(tracked->filled_quantity < 100);
    FillLogger::log_order_state(*tracked, "after first partial");

    // Continue matching until fully filled (may take several cycles because
    // partial_fill_probability is 1.0, but each partial moves forward).
    int max_cycles = 50;
    int cycle = 0;
    while (max_cycles-- > 0)
    {
        ++cycle;
        exchange.process_matching();
        tracked = handler.get_order(1);
        if (tracked->state == OrderState::Filled)
        {
            std::cout << "[FILL] Fully filled after " << cycle << " additional matching cycles\n";
            break;
        }
    }

    tracked = handler.get_order(1);
    REQUIRE(tracked != nullptr);
    CHECK_WITH_LOG(tracked->state == OrderState::Filled, "order fully filled after partials");
    CHECK_WITH_LOG(tracked->filled_quantity == 100, "total fill qty = 100");
    CHECK_WITH_LOG(tracked->remaining_quantity == 0, "no remaining quantity");
    FillLogger::log_order_state(*tracked, "final state");
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
        FillLogger::log_exec_report(rpt, "exec callback");
        all_reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(2, 200, 151.0);
    std::cout << "[TEST] Created buy order: id=2, qty=200, px=151.0\n";
    StateTransitionLogger::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // Run matching until filled.
    int max_cycles = 100;
    int cycle = 0;
    while (max_cycles-- > 0)
    {
        ++cycle;
        exchange.process_matching();
        const Order *tracked = handler.get_order(2);
        if (tracked && tracked->state == OrderState::Filled)
        {
            std::cout << "[FILL] Filled after " << cycle << " matching cycles\n";
            break;
        }
    }

    // Verify cumulative quantities in reports are monotonically increasing.
    Quantity prev_cum = 0;
    int fill_report_count = 0;
    for (const auto &r : all_reports)
    {
        if (r.order_id != 2)
            continue;
        if (r.last_quantity > 0)
        {
            CHECK(r.cum_quantity > prev_cum);
            prev_cum = r.cum_quantity;
            ++fill_report_count;
        }
    }
    std::cout << "[FILL] Total fill reports with last_quantity > 0: " << fill_report_count << "\n";

    // Final state.
    const Order *tracked = handler.get_order(2);
    REQUIRE(tracked != nullptr);
    CHECK_WITH_LOG(tracked->state == OrderState::Filled, "order fully filled");
    CHECK_WITH_LOG(tracked->filled_quantity == 200, "total fill qty = 200");
    CHECK_WITH_LOG(tracked->remaining_quantity == 0, "no remaining quantity");
    // Average fill price should be valid.
    CHECK_WITH_LOG(tracked->avg_fill_price > 0, "valid average fill price");
    FillLogger::log_order_state(*tracked, "final state");
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
                                    {
        FillLogger::log_exec_report(rpt, "exec callback");
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(3, 100, 151.0);
    std::cout << "[TEST] Created buy order for avg price test: id=3, qty=100\n";
    StateTransitionLogger::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    exchange.process_matching();

    const Order *tracked = handler.get_order(3);
    REQUIRE(tracked != nullptr);
    CHECK_WITH_LOG(tracked->state == OrderState::Filled, "order filled");
    // All filled at market ask = 150.0.
    CHECK_WITH_LOG(tracked->avg_fill_price == to_price(150.0), "avg fill price = 150.0");
    FillLogger::log_order_state(*tracked, "final state");
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
                                    {
        FillLogger::log_exec_report(rpt, "exec callback");
        handler.on_execution_report(rpt); });

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

    std::cout << "[TEST] Created parent order: id=100, qty=200\n";
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

    std::cout << "[TEST] Created child1 (id=201) and child2 (id=202), each qty=100\n";
    handler.track_child_order(100, child1);
    handler.track_child_order(100, child2);

    // Send child1, fill it.
    std::cout << "[TEST] Sending child1 (id=201) to exchange\n";
    REQUIRE(exchange.send_order(child1));
    exchange.process_matching();

    // Parent should be PartiallyFilled after first child fills.
    const Order *tracked_parent = handler.get_order(100);
    REQUIRE(tracked_parent != nullptr);
    CHECK_WITH_LOG(tracked_parent->filled_quantity == 100, "parent half filled after child1");
    CHECK_WITH_LOG(tracked_parent->remaining_quantity == 100, "parent has 100 remaining");
    FillLogger::log_order_state(*tracked_parent, "after child1 fill");

    // Send and fill child2.
    std::cout << "[TEST] Sending child2 (id=202) to exchange\n";
    REQUIRE(exchange.send_order(child2));
    exchange.process_matching();

    // Parent should now be Filled.
    tracked_parent = handler.get_order(100);
    REQUIRE(tracked_parent != nullptr);
    CHECK_WITH_LOG(tracked_parent->state == OrderState::Filled, "parent fully filled");
    CHECK_WITH_LOG(tracked_parent->filled_quantity == 200, "parent total fill = 200");
    CHECK_WITH_LOG(tracked_parent->remaining_quantity == 0, "parent no remaining");
    CHECK_WITH_LOG(tracked_parent->avg_fill_price == to_price(150.0), "parent avg price = 150.0");
    FillLogger::log_order_state(*tracked_parent, "parent final state");
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
    handler.set_fill_callback([&](const Order &o, const ExecutionReport &rpt)
                              {
        ++fill_callback_count;
        std::cout << "[FILL] Fill callback #" << fill_callback_count
                  << ": order_id=" << o.id
                  << " last_qty=" << rpt.last_quantity
                  << " cum_qty=" << rpt.cum_quantity << "\n";
        CHECK(rpt.last_quantity > 0); });

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_buy(4, 100, 151.0);
    std::cout << "[TEST] Created buy order for fill callback test: id=4, qty=100\n";
    StateTransitionLogger::apply(order, OrderEvent::Submit);
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
    std::cout << "[FILL] Total fill callbacks: " << fill_callback_count << "\n";
    CHECK_WITH_LOG(fill_callback_count >= 2, "received multiple fill callbacks");
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
    StateTransitionLogger::apply(order, OrderEvent::Submit);
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
    std::cout << "[TEST] Handler stats: total_fills=" << stats.total_fills
              << " total_partial_fills=" << stats.total_partial_fills << "\n";
    CHECK(stats.total_fills >= 1);
    CHECK(stats.total_partial_fills >= 1);
}
