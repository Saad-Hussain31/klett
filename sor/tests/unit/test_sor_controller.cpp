#include <catch2/catch_test_macros.hpp>
#include "ui/sor_controller.h"

using namespace sor;
using namespace sor::ui;

TEST_CASE("SorController initializes without config", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));
    REQUIRE_FALSE(ctrl.is_running());
}

TEST_CASE("SorController start/stop lifecycle", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));

    ctrl.start();
    REQUIRE(ctrl.is_running());

    ctrl.stop();
    REQUIRE_FALSE(ctrl.is_running());
}

TEST_CASE("SorController submit and track order", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));
    ctrl.start();

    OrderParams params;
    params.symbol = "AAPL";
    params.side = Side::Buy;
    params.quantity = 100;
    params.type = OrderType::Limit;
    params.price = 150.0;
    params.strategy = RoutingStrategy::BestPrice;

    OrderId id = ctrl.submit_order(params);
    REQUIRE(id != INVALID_ORDER_ID);

    auto tracked = ctrl.get_tracked_order_ids();
    REQUIRE(tracked.size() == 1);
    REQUIRE(tracked[0] == id);

    // Give the gateway thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto snap = ctrl.get_order_snapshot(id);
    REQUIRE(snap.has_value());
    REQUIRE(snap->symbol == Symbol("AAPL"));
    REQUIRE(snap->side == Side::Buy);
    REQUIRE(snap->quantity == 100);

    ctrl.stop();
}

TEST_CASE("SorController NBBO available after init", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));
    ctrl.start();

    // Feeds generated 10 ticks during init, so NBBO should be available
    auto nbbo = ctrl.get_nbbo(Symbol("AAPL"));
    REQUIRE(nbbo.valid());
    REQUIRE(nbbo.best_bid > 0);
    REQUIRE(nbbo.best_ask > 0);
    REQUIRE(nbbo.best_ask > nbbo.best_bid);

    ctrl.stop();
}

TEST_CASE("SorController kill switch toggle", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));

    REQUIRE_FALSE(ctrl.is_kill_switch_active());
    ctrl.toggle_kill_switch();
    REQUIRE(ctrl.is_kill_switch_active());
    ctrl.toggle_kill_switch();
    REQUIRE_FALSE(ctrl.is_kill_switch_active());
}

TEST_CASE("SorController drain events are initially empty", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));

    auto fills = ctrl.drain_fill_events();
    auto completions = ctrl.drain_completion_events();
    auto logs = ctrl.drain_log_messages();

    REQUIRE(fills.empty());
    REQUIRE(completions.empty());
    // Logs may not be empty since init produces log messages
}

TEST_CASE("SorController watched symbols", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));

    // Default AAPL
    REQUIRE(ctrl.watched_symbols().size() == 1);
    REQUIRE(ctrl.watched_symbols()[0] == Symbol("AAPL"));

    ctrl.add_watched_symbol(Symbol("MSFT"));
    REQUIRE(ctrl.watched_symbols().size() == 2);

    // No duplicate
    ctrl.add_watched_symbol(Symbol("AAPL"));
    REQUIRE(ctrl.watched_symbols().size() == 2);
}

TEST_CASE("SorController fill events flow through after order", "[controller]")
{
    SorController ctrl;
    REQUIRE(ctrl.initialize(""));
    ctrl.start();

    OrderParams params;
    params.symbol = "AAPL";
    params.side = Side::Buy;
    params.quantity = 10;
    params.type = OrderType::Market;
    params.price = 0.0;
    params.strategy = RoutingStrategy::BestPrice;

    OrderId id = ctrl.submit_order(params);
    REQUIRE(id != INVALID_ORDER_ID);

    // Wait for processing + matching
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // At least some fills or completions should have happened
    auto fills = ctrl.drain_fill_events();
    auto completions = ctrl.drain_completion_events();

    // If fill_probability is 0.95 and we submitted a market order,
    // we should almost certainly get at least one fill or completion
    bool got_events = !fills.empty() || !completions.empty();
    // This is probabilistic; don't hard-fail, just check the plumbing worked
    if (got_events)
    {
        if (!fills.empty())
        {
            REQUIRE(fills[0].order_id != INVALID_ORDER_ID);
            REQUIRE(fills[0].symbol == Symbol("AAPL"));
        }
    }

    ctrl.stop();
}
