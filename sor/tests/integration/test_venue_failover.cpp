// Integration test: venue failover -- order routes to healthy venue
// when one goes down.

#include <catch2/catch_test_macros.hpp>

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

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static SimulatedExchange make_exchange(VenueId id)
{
    SimulatedExchange::Config cfg;
    cfg.venue_id = id;
    cfg.name = "Exchange" + std::to_string(id);
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
// Venue failover
// ============================================================================

TEST_CASE("Failover: disconnected venue rejects, healthy venue accepts",
          "[integration][failover]")
{
    auto exchange1 = make_exchange(1);
    auto exchange2 = make_exchange(2);

    // Only connect exchange2; exchange1 is down.
    // exchange1 not connected.
    REQUIRE(exchange2.connect());
    REQUIRE_FALSE(exchange1.is_connected());
    std::cout << "[TEST] Exchange1: disconnected, Exchange2: connected\n";

    exchange2.set_market_price(to_price(149.0), to_price(150.0));

    ExecutionHandler handler;
    std::vector<ExecutionReport> reports;

    exchange2.set_execution_callback([&](const ExecutionReport &rpt)
                                     {
        FillLogger::log_exec_report(rpt, "exchange2 callback");
        reports.push_back(rpt);
        handler.on_execution_report(rpt); });

    auto order = make_buy(1, 100, 151.0);
    std::cout << "[TEST] Created buy order: id=1, qty=100, px=151.0\n";
    StateTransitionLogger::apply(order, OrderEvent::Submit);
    handler.track_order(order);

    // Attempt to send to disconnected exchange -- should fail.
    bool sent1 = exchange1.send_order(order);
    std::cout << "[TEST] Send to exchange1 (down): " << (sent1 ? "success" : "FAILED (expected)") << "\n";
    REQUIRE_FALSE(sent1);

    // Fall back to healthy exchange2.
    bool sent2 = exchange2.send_order(order);
    std::cout << "[TEST] Send to exchange2 (up): " << (sent2 ? "success" : "FAILED") << "\n";
    REQUIRE(sent2);

    exchange2.process_matching();

    // Verify fill came from exchange2.
    bool filled = false;
    for (const auto &r : reports)
    {
        if (r.state == OrderState::Filled)
        {
            filled = true;
            CHECK(r.venue_id == 2);
            CHECK(r.cum_quantity == 100);
            FillLogger::log_exec_report(r, "fill from exchange2");
        }
    }
    REQUIRE(filled);

    const Order *tracked = handler.get_order(1);
    REQUIRE(tracked != nullptr);
    CHECK_WITH_LOG(tracked->state == OrderState::Filled, "order filled via failover");
    FillLogger::log_order_state(*tracked, "final state");
}

TEST_CASE("Failover: venue goes down mid-session", "[integration][failover]")
{
    auto exchange1 = make_exchange(1);
    auto exchange2 = make_exchange(2);

    REQUIRE(exchange1.connect());
    REQUIRE(exchange2.connect());
    std::cout << "[TEST] Both exchanges connected\n";

    exchange1.set_market_price(to_price(149.0), to_price(150.0));
    exchange2.set_market_price(to_price(149.0), to_price(150.0));

    ExecutionHandler handler;
    std::vector<ExecutionReport> reports1;
    std::vector<ExecutionReport> reports2;

    exchange1.set_execution_callback([&](const ExecutionReport &rpt)
                                     {
        FillLogger::log_exec_report(rpt, "exchange1 callback");
        reports1.push_back(rpt);
        handler.on_execution_report(rpt); });

    exchange2.set_execution_callback([&](const ExecutionReport &rpt)
                                     {
        FillLogger::log_exec_report(rpt, "exchange2 callback");
        reports2.push_back(rpt);
        handler.on_execution_report(rpt); });

    // Send first order to exchange1 -- succeeds.
    auto order1 = make_buy(10, 100, 151.0);
    std::cout << "[TEST] Order1: id=10, sending to exchange1\n";
    StateTransitionLogger::apply(order1, OrderEvent::Submit);
    handler.track_order(order1);
    REQUIRE(exchange1.send_order(order1));
    exchange1.process_matching();

    CHECK_WITH_LOG(handler.get_order(10)->state == OrderState::Filled, "order1 filled on exchange1");

    // Now exchange1 goes down.
    exchange1.disconnect();
    REQUIRE_FALSE(exchange1.is_connected());
    std::cout << "[TEST] Exchange1 disconnected mid-session\n";
    CHECK(exchange1.status() == VenueStatus::Disconnected);

    // Second order cannot go to exchange1.
    auto order2 = make_buy(11, 50, 151.0);
    std::cout << "[TEST] Order2: id=11, attempting exchange1 (down)\n";
    StateTransitionLogger::apply(order2, OrderEvent::Submit);
    handler.track_order(order2);

    REQUIRE_FALSE(exchange1.send_order(order2));
    std::cout << "[TEST] Exchange1 rejected (disconnected), routing to exchange2\n";

    // Route to exchange2 instead.
    REQUIRE(exchange2.send_order(order2));
    exchange2.process_matching();

    const Order *tracked = handler.get_order(11);
    REQUIRE(tracked != nullptr);
    CHECK_WITH_LOG(tracked->state == OrderState::Filled, "order2 filled via failover to exchange2");
    CHECK_WITH_LOG(tracked->filled_quantity == 50, "correct fill quantity");
    FillLogger::log_order_state(*tracked, "final state");
}

TEST_CASE("Failover: reroute callback on child reject", "[integration][failover]")
{
    // Use an exchange with 100% reject rate to simulate failure.
    SimulatedExchange::Config bad_cfg;
    bad_cfg.venue_id = 1;
    bad_cfg.name = "BadExchange";
    bad_cfg.latency = std::chrono::microseconds(0);
    bad_cfg.latency_jitter = std::chrono::microseconds(0);
    bad_cfg.fill_probability = 1.0;
    bad_cfg.partial_fill_probability = 0.0;
    bad_cfg.reject_probability = 1.0; // always reject
    bad_cfg.fee_rate = 0.001;

    SimulatedExchange bad_exchange(bad_cfg);
    auto good_exchange = make_exchange(2);

    REQUIRE(bad_exchange.connect());
    REQUIRE(good_exchange.connect());
    std::cout << "[TEST] BadExchange (reject_prob=1.0) and GoodExchange connected\n";

    bad_exchange.set_market_price(to_price(149.0), to_price(150.0));
    good_exchange.set_market_price(to_price(149.0), to_price(150.0));

    ExecutionHandler handler;

    // Track reroute events.
    int reroute_count = 0;
    handler.set_reroute_callback([&](Order &parent)
                                 {
        ++reroute_count;
        std::cout << "[TEST] Reroute callback fired for parent_id=" << parent.id
                  << " (reroute #" << reroute_count << ")\n";
        // On reroute, send to good exchange.
        Order child{};
        child.id = parent.id + 1000;
        child.client_order_id = child.id;
        child.symbol = parent.symbol;
        child.side = parent.side;
        child.type = parent.type;
        child.tif = parent.tif;
        child.price = parent.price;
        child.quantity = parent.remaining_quantity;
        child.remaining_quantity = parent.remaining_quantity;
        child.state = OrderState::PendingNew;
        child.create_time = std::chrono::steady_clock::now();

        handler.track_child_order(parent.id, child);
        good_exchange.send_order(child); });

    bad_exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                        {
        FillLogger::log_exec_report(rpt, "bad_exchange callback");
        handler.on_execution_report(rpt); });

    good_exchange.set_execution_callback([&](const ExecutionReport &rpt)
                                         {
        FillLogger::log_exec_report(rpt, "good_exchange callback");
        handler.on_execution_report(rpt); });

    // Create parent order.
    auto parent = make_buy(100, 100, 151.0);
    std::cout << "[TEST] Created parent order: id=100, qty=100\n";
    StateTransitionLogger::apply(parent, OrderEvent::Submit);
    handler.track_order(parent);

    // Create child targeting bad exchange.
    Order child{};
    child.id = 200;
    child.client_order_id = 200;
    child.parent_order_id = 100;
    child.symbol = parent.symbol;
    child.side = parent.side;
    child.type = parent.type;
    child.tif = parent.tif;
    child.price = parent.price;
    child.quantity = 100;
    child.remaining_quantity = 100;
    child.state = OrderState::PendingNew;
    child.create_time = std::chrono::steady_clock::now();

    std::cout << "[TEST] Sending child id=200 to bad_exchange\n";
    handler.track_child_order(100, child);
    REQUIRE(bad_exchange.send_order(child));

    // Process matching on bad exchange -- will reject.
    bad_exchange.process_matching();

    // The reroute callback should have fired.
    CHECK_WITH_LOG(reroute_count >= 1, "reroute callback fired");

    // Process the good exchange to fill the rerouted child.
    good_exchange.process_matching();

    // Check that parent eventually gets filled.
    const Order *tracked_parent = handler.get_order(100);
    REQUIRE(tracked_parent != nullptr);
    CHECK_WITH_LOG(tracked_parent->filled_quantity == 100, "parent fully filled after reroute");
    FillLogger::log_order_state(*tracked_parent, "parent final state");
}
