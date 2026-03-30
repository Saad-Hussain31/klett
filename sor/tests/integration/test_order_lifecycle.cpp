// Integration test: full order lifecycle through SimulatedExchange + ExecutionHandler.

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

/// Create a SimulatedExchange with deterministic behaviour (no randomness).
static SimulatedExchange make_deterministic_exchange(VenueId id = 1)
{
    SimulatedExchange::Config cfg;
    cfg.venue_id = id;
    cfg.name = "TestExchange";
    cfg.latency = std::chrono::microseconds(0);
    cfg.latency_jitter = std::chrono::microseconds(0);
    cfg.fill_probability = 1.0;         // always fill
    cfg.partial_fill_probability = 0.0; // no partials by default
    cfg.reject_probability = 0.0;       // no random rejects
    cfg.fee_rate = 0.001;
    return SimulatedExchange(cfg);
}

static Order make_limit_buy(OrderId id, const char *sym, Quantity qty, double price)
{
    Order o{};
    o.id = id;
    o.client_order_id = id;
    o.symbol = sym;
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

static Order make_limit_sell(OrderId id, const char *sym, Quantity qty, double price)
{
    Order o = make_limit_buy(id, sym, qty, price);
    o.side = Side::Sell;
    return o;
}

// ============================================================================
// Full lifecycle: submit -> accept -> fill -> Filled
// ============================================================================

TEST_CASE("Lifecycle: submit limit buy, full fill", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);
    ExecutionHandler handler;

    // Collect execution reports.
    std::vector<ExecutionReport> reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());

    // Set market price so our buy limit is marketable.
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    // Create and track the order.
    auto order = make_limit_buy(1, "AAPL", 100, 151.0);

    // Submit -> PendingNew.
    OrderStateMachine::apply(order, OrderEvent::Submit);
    REQUIRE(order.state == OrderState::PendingNew);

    handler.track_order(order);

    // Send to exchange -- this generates an Accepted ack synchronously.
    REQUIRE(exchange.send_order(order));

    // We should have received an Accepted report.
    REQUIRE(reports.size() >= 1);
    CHECK(reports[0].state == OrderState::Accepted);

    // Process matching -- should generate a fill.
    exchange.process_matching();

    // Check that we received a Filled report.
    REQUIRE(reports.size() >= 2);

    // Find the terminal report.
    bool found_fill = false;
    for (const auto &r : reports)
    {
        if (r.state == OrderState::Filled)
        {
            found_fill = true;
            CHECK(r.cum_quantity == 100);
            CHECK(r.leaves_quantity == 0);
            CHECK(r.last_price == to_price(150.0));
            CHECK(r.last_quantity == 100);
        }
    }
    REQUIRE(found_fill);

    // Verify order state in handler.
    const Order *tracked = handler.get_order(1);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Filled);
    CHECK(tracked->filled_quantity == 100);
    CHECK(tracked->remaining_quantity == 0);
}

TEST_CASE("Lifecycle: submit limit sell, full fill", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);
    ExecutionHandler handler;

    std::vector<ExecutionReport> reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(150.0), to_price(151.0));

    auto order = make_limit_sell(2, "AAPL", 50, 149.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    exchange.process_matching();

    const Order *tracked = handler.get_order(2);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Filled);
    CHECK(tracked->filled_quantity == 50);
}

// ============================================================================
// Non-marketable order stays live
// ============================================================================

TEST_CASE("Lifecycle: non-marketable limit stays Accepted", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);
    ExecutionHandler handler;

    std::vector<ExecutionReport> reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    // Market ask at 155; our buy limit at 150 is not marketable.
    exchange.set_market_price(to_price(149.0), to_price(155.0));

    auto order = make_limit_buy(3, "AAPL", 100, 150.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    exchange.process_matching();

    const Order *tracked = handler.get_order(3);
    REQUIRE(tracked != nullptr);
    // Should remain Accepted (not filled; non-marketable GTC order stays on book).
    CHECK(tracked->state == OrderState::Accepted);
    CHECK(tracked->filled_quantity == 0);
}

// ============================================================================
// Cancel lifecycle
// ============================================================================

TEST_CASE("Lifecycle: submit then cancel", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);
    ExecutionHandler handler;

    std::vector<ExecutionReport> reports;
    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    {
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    // Non-marketable so order sits live.
    exchange.set_market_price(to_price(149.0), to_price(155.0));

    auto order = make_limit_buy(4, "AAPL", 100, 150.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    // Request cancellation.
    CancelRequest cr{};
    cr.order_id = 4;
    cr.symbol = "AAPL";
    cr.side = Side::Buy;
    REQUIRE(exchange.cancel_order(cr));

    // Process -- cancel should be acknowledged.
    exchange.process_matching();

    bool found_cancel = false;
    for (const auto &r : reports)
    {
        if (r.state == OrderState::Canceled)
        {
            found_cancel = true;
        }
    }
    REQUIRE(found_cancel);

    const Order *tracked = handler.get_order(4);
    REQUIRE(tracked != nullptr);
    CHECK(tracked->state == OrderState::Canceled);
}

// ============================================================================
// Completion callback fires
// ============================================================================

TEST_CASE("Lifecycle: completion callback fires on full fill", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);
    ExecutionHandler handler;

    bool completion_fired = false;
    handler.set_completion_callback([&](const Order &o)
                                    {
        completion_fired = true;
        CHECK(o.state == OrderState::Filled);
        CHECK(o.filled_quantity == 100); });

    exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                    { handler.on_execution_report(rpt); });

    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_limit_buy(5, "AAPL", 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    handler.track_order(order);
    REQUIRE(exchange.send_order(order));

    exchange.process_matching();

    CHECK(completion_fired);
}

// ============================================================================
// Statistics tracking
// ============================================================================

TEST_CASE("Lifecycle: exchange stats track fills", "[integration][lifecycle]")
{
    auto exchange = make_deterministic_exchange(1);

    exchange.set_execution_callback([](const ExecutionReport &) {});
    REQUIRE(exchange.connect());
    exchange.set_market_price(to_price(149.0), to_price(150.0));

    auto order = make_limit_buy(6, "AAPL", 100, 151.0);
    OrderStateMachine::apply(order, OrderEvent::Submit);
    REQUIRE(exchange.send_order(order));
    exchange.process_matching();

    auto stats = exchange.get_stats();
    CHECK(stats.orders_received == 1);
    CHECK(stats.orders_filled == 1);
}
