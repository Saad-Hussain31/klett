// Unit tests for routing strategies: BestPrice, LiquiditySweep, SmartIOC, VWAP.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "routing/best_price.h"
#include "routing/liquidity_sweep.h"
#include "routing/smart_ioc.h"
#include "routing/vwap.h"
#include "market_data/aggregator.h"
#include "market_data/book.h"
#include "core/types.h"
#include "core/order.h"

#include <chrono>
#include <algorithm>
#include <numeric>

using namespace sor;
using namespace sor::routing;
using namespace sor::market_data;

// ---------------------------------------------------------------------------
// Helpers to build test market data
// ---------------------------------------------------------------------------

static Order make_order(Side side, Quantity qty, double price,
                        OrderType type = OrderType::Limit,
                        TimeInForce tif = TimeInForce::GTC)
{
    Order o{};
    o.id = 1;
    o.symbol = "AAPL";
    o.side = side;
    o.type = type;
    o.tif = tif;
    o.price = to_price(price);
    o.quantity = qty;
    o.remaining_quantity = qty;
    o.filled_quantity = 0;
    o.state = OrderState::Accepted;
    return o;
}

/// Build an NBBO with given bid/ask and venue attribution.
static NBBO make_nbbo(double bid, Quantity bid_qty, VenueId bid_venue,
                      double ask, Quantity ask_qty, VenueId ask_venue)
{
    NBBO nbbo{};
    nbbo.best_bid = to_price(bid);
    nbbo.best_bid_qty = bid_qty;
    nbbo.best_bid_venue = bid_venue;
    nbbo.best_ask = to_price(ask);
    nbbo.best_ask_qty = ask_qty;
    nbbo.best_ask_venue = ask_venue;
    nbbo.timestamp = std::chrono::steady_clock::now();
    return nbbo;
}

/// Helper to add a level with venue attribution to an AggregatedBook.
static void add_ask_level(AggregatedBook &book, double price, VenueId venue,
                          Quantity qty)
{
    auto &lvl = book.asks[book.ask_depth];
    lvl.price = to_price(price);
    lvl.total_quantity = 0;
    lvl.venue_count = 0;
    lvl.add_venue(venue, qty);
    ++book.ask_depth;
}

static void add_bid_level(AggregatedBook &book, double price, VenueId venue,
                          Quantity qty)
{
    auto &lvl = book.bids[book.bid_depth];
    lvl.price = to_price(price);
    lvl.total_quantity = 0;
    lvl.venue_count = 0;
    lvl.add_venue(venue, qty);
    ++book.bid_depth;
}

/// Add a venue contribution to an existing aggregated level.
static void add_venue_to_ask(AggregatedBook &book, size_t level_idx,
                             VenueId venue, Quantity qty)
{
    book.asks[level_idx].add_venue(venue, qty);
}

static VenueScore make_venue_score(VenueId id, double fee = 0.001,
                                   double latency = 100.0,
                                   double fill_rate = 0.95)
{
    VenueScore vs{};
    vs.venue_id = id;
    vs.fee_rate = fee;
    vs.latency_us = latency;
    vs.fill_rate = fill_rate;
    vs.is_available = true;
    return vs;
}

// ============================================================================
// BestPriceStrategy
// ============================================================================

TEST_CASE("BestPrice: routes buy to lowest ask venue", "[routing][best_price]")
{
    BestPriceStrategy strategy;

    auto order = make_order(Side::Buy, 100, 155.0);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 200, 2);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Venue 1 has ask at 150.50, venue 2 has ask at 150.00.
    add_ask_level(book, 150.00, 2, 200);
    add_ask_level(book, 150.50, 1, 200);

    std::vector<VenueScore> venues = {
        make_venue_score(1, 0.001, 100.0, 0.95),
        make_venue_score(2, 0.001, 100.0, 0.95),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    REQUIRE(decision.slices.size() == 1);
    // Venue 2 has the best ask (150.00).
    CHECK(decision.slices[0].venue_id == 2);
    CHECK(decision.slices[0].price == to_price(150.00));
    CHECK(decision.slices[0].quantity == 100);
}

TEST_CASE("BestPrice: routes sell to highest bid venue", "[routing][best_price]")
{
    BestPriceStrategy strategy;

    auto order = make_order(Side::Sell, 50, 148.0);
    auto nbbo = make_nbbo(150.0, 100, 1, 151.0, 100, 2);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Venue 1: bid at 150.0, Venue 2: bid at 149.5.
    add_bid_level(book, 150.0, 1, 100);
    add_bid_level(book, 149.5, 2, 100);

    std::vector<VenueScore> venues = {
        make_venue_score(1, 0.001, 100.0, 0.95),
        make_venue_score(2, 0.001, 100.0, 0.95),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    REQUIRE(decision.slices.size() == 1);
    CHECK(decision.slices[0].venue_id == 1);
    CHECK(decision.slices[0].price == to_price(150.0));
}

TEST_CASE("BestPrice: considers fees for buy", "[routing][best_price]")
{
    BestPriceStrategy strategy;

    auto order = make_order(Side::Buy, 100, 155.0);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 200, 2);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Both venues at same raw price, but venue 1 has lower fees.
    add_ask_level(book, 150.0, 1, 200); // level 0
    add_venue_to_ask(book, 0, 2, 200);  // same price, different venue

    std::vector<VenueScore> venues = {
        make_venue_score(1, 0.0001, 100.0, 0.95), // very low fee
        make_venue_score(2, 0.01, 100.0, 0.95),   // high fee
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    REQUIRE(decision.slices.size() == 1);
    // Should prefer venue 1 (lower fee-adjusted cost).
    CHECK(decision.slices[0].venue_id == 1);
}

TEST_CASE("BestPrice: returns empty decision with no venues", "[routing][best_price]")
{
    BestPriceStrategy strategy;

    auto order = make_order(Side::Buy, 100, 155.0);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 200, 2);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    std::vector<VenueScore> venues; // empty
    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE_FALSE(decision.valid());
}

TEST_CASE("BestPrice: returns empty decision with invalid NBBO", "[routing][best_price]")
{
    BestPriceStrategy strategy;

    auto order = make_order(Side::Buy, 100, 155.0);
    NBBO nbbo{}; // default -- invalid
    AggregatedBook book{};

    std::vector<VenueScore> venues = {make_venue_score(1)};
    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE_FALSE(decision.valid());
}

// ============================================================================
// LiquiditySweepStrategy
// ============================================================================

TEST_CASE("LiquiditySweep: sweeps multiple venues", "[routing][liquidity_sweep]")
{
    LiquiditySweepStrategy strategy;

    auto order = make_order(Side::Buy, 300, 155.0);
    auto nbbo = make_nbbo(149.0, 100, 1, 150.0, 100, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Two venues at the same price level.
    add_ask_level(book, 150.0, 1, 200); // level 0
    add_venue_to_ask(book, 0, 2, 200);  // same level, different venue

    std::vector<VenueScore> venues = {
        make_venue_score(1, 0.001),
        make_venue_score(2, 0.001),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    // Should have slices for both venues.
    REQUIRE(decision.slices.size() >= 1);

    // Total allocated should match order quantity (or be close; all at one level).
    Quantity total = decision.total_quantity();
    CHECK(total == 300);
}

TEST_CASE("LiquiditySweep: total quantity matches order", "[routing][liquidity_sweep]")
{
    LiquiditySweepStrategy strategy;

    auto order = make_order(Side::Buy, 150, 155.0);
    auto nbbo = make_nbbo(149.0, 100, 1, 150.0, 100, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Enough liquidity at one level.
    add_ask_level(book, 150.0, 1, 100);
    add_ask_level(book, 150.5, 2, 100);

    std::vector<VenueScore> venues = {
        make_venue_score(1),
        make_venue_score(2),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    CHECK(decision.total_quantity() == 150);
}

TEST_CASE("LiquiditySweep: sweeps across multiple price levels", "[routing][liquidity_sweep]")
{
    LiquiditySweepStrategy strategy;

    auto order = make_order(Side::Buy, 250, 155.0);
    auto nbbo = make_nbbo(149.0, 100, 1, 150.0, 100, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Level 0: 100 @ 150.0 on venue 1
    add_ask_level(book, 150.0, 1, 100);
    // Level 1: 200 @ 150.5 on venue 2
    add_ask_level(book, 150.5, 2, 200);

    std::vector<VenueScore> venues = {
        make_venue_score(1),
        make_venue_score(2),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    CHECK(decision.total_quantity() == 250);
    CHECK(decision.slices.size() == 2);
}

// ============================================================================
// SmartIOCStrategy
// ============================================================================

TEST_CASE("SmartIOC: FOK requires single venue fill", "[routing][smart_ioc]")
{
    // Slippage wide enough to cover our test price levels.
    SmartIOCStrategy strategy(to_price(1.0));

    auto order = make_order(Side::Buy, 100, 155.0, OrderType::Limit, TimeInForce::FOK);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 200, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    SECTION("Venue has enough -- succeeds")
    {
        add_ask_level(book, 150.0, 1, 200);

        std::vector<VenueScore> venues = {
            make_venue_score(1, 0.001, 100.0, 0.95),
        };

        auto decision = strategy.route(order, nbbo, book, venues);
        REQUIRE(decision.valid());
        REQUIRE(decision.slices.size() == 1);
        CHECK(decision.slices[0].venue_id == 1);
        CHECK(decision.slices[0].quantity == 100);
        CHECK(decision.slices[0].tif == TimeInForce::FOK);
    }

    SECTION("No single venue has enough -- fails (empty decision)")
    {
        // Two venues, each with 60. No single venue can fill 100.
        add_ask_level(book, 150.0, 1, 60);
        add_ask_level(book, 150.2, 2, 60);

        std::vector<VenueScore> venues = {
            make_venue_score(1, 0.001, 100.0, 0.95),
            make_venue_score(2, 0.001, 200.0, 0.90),
        };

        auto decision = strategy.route(order, nbbo, book, venues);
        REQUIRE_FALSE(decision.valid());
    }
}

TEST_CASE("SmartIOC: IOC sweeps available liquidity", "[routing][smart_ioc]")
{
    // Slippage in Price-fixed-point units. to_price(1.0) = 1e8, so
    // a slippage of 1e8 ticks allows up to $1 beyond NBBO.
    SmartIOCStrategy strategy(to_price(1.0));

    auto order = make_order(Side::Buy, 150, 155.0, OrderType::Limit, TimeInForce::IOC);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 100, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Two levels with liquidity within the $1 slippage band.
    add_ask_level(book, 150.0, 1, 80);
    add_ask_level(book, 150.2, 2, 100);

    std::vector<VenueScore> venues = {
        make_venue_score(1, 0.001, 50.0, 0.95),  // fast venue
        make_venue_score(2, 0.001, 200.0, 0.90), // slower venue
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    CHECK(decision.total_quantity() == 150);

    // All slices should use IOC time-in-force.
    for (const auto &slice : decision.slices)
    {
        CHECK(slice.tif == TimeInForce::IOC);
    }
}

TEST_CASE("SmartIOC: respects slippage tolerance", "[routing][smart_ioc]")
{
    // Set very tight slippage (1 tick).
    SmartIOCStrategy strategy(1);

    auto order = make_order(Side::Buy, 200, 155.0, OrderType::Limit, TimeInForce::IOC);
    auto nbbo = make_nbbo(149.0, 200, 1, 150.0, 100, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;

    // Level 0 within slippage, level 1 beyond.
    add_ask_level(book, 150.0, 1, 50);
    // to_price(150.0) + 1 tick = best_ask + 1
    // Anything beyond best_ask + 1 tick is out.
    add_ask_level(book, 155.0, 2, 200);

    std::vector<VenueScore> venues = {
        make_venue_score(1),
        make_venue_score(2),
    };

    auto decision = strategy.route(order, nbbo, book, venues);
    // Should only fill from level 0 (within slippage).
    if (decision.valid())
    {
        CHECK(decision.total_quantity() <= 50);
    }
}

// ============================================================================
// VWAPStrategy
// ============================================================================

TEST_CASE("VWAP: produces a slice with valid market data", "[routing][vwap]")
{
    VWAPStrategy::Config cfg;
    cfg.duration = std::chrono::minutes(1);
    cfg.num_slices = 10;
    cfg.urgency = 0.5;
    cfg.max_participation_rate = 0.25;

    VWAPStrategy strategy(cfg);

    auto order = make_order(Side::Buy, 1000, 155.0);
    auto nbbo = make_nbbo(149.0, 500, 1, 150.0, 500, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;
    add_ask_level(book, 150.0, 1, 500);

    std::vector<VenueScore> venues = {
        make_venue_score(1),
    };

    strategy.begin(1000, std::chrono::steady_clock::now());

    auto decision = strategy.get_next_slice(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    REQUIRE(decision.slices.size() == 1);
    // Slice quantity should be a fraction of total.
    CHECK(decision.slices[0].quantity > 0);
    CHECK(decision.slices[0].quantity <= 1000);
}

TEST_CASE("VWAP: multiple slices accumulate fills", "[routing][vwap]")
{
    VWAPStrategy::Config cfg;
    cfg.duration = std::chrono::minutes(1);
    cfg.num_slices = 5;
    cfg.urgency = 0.5;
    cfg.max_participation_rate = 1.0;

    VWAPStrategy strategy(cfg);

    auto order = make_order(Side::Buy, 500, 155.0);
    auto nbbo = make_nbbo(149.0, 500, 1, 150.0, 500, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;
    add_ask_level(book, 150.0, 1, 500);

    std::vector<VenueScore> venues = {make_venue_score(1)};

    strategy.begin(500, std::chrono::steady_clock::now());
    REQUIRE_FALSE(strategy.is_complete());

    Quantity total_sliced = 0;
    for (int i = 0; i < 10 && !strategy.is_complete(); ++i)
    {
        auto decision = strategy.get_next_slice(order, nbbo, book, venues);
        if (decision.valid())
        {
            Quantity filled = decision.slices[0].quantity;
            strategy.on_fill(filled);
            total_sliced += filled;
        }
    }

    // Should have filled 500 total.
    CHECK(total_sliced == 500);
    CHECK(strategy.is_complete());
}

TEST_CASE("VWAP: fill_progress and is_complete", "[routing][vwap]")
{
    VWAPStrategy strategy;
    strategy.begin(100, std::chrono::steady_clock::now());

    CHECK(strategy.fill_progress() == Catch::Approx(0.0));
    CHECK_FALSE(strategy.is_complete());

    strategy.on_fill(50);
    CHECK(strategy.fill_progress() == Catch::Approx(0.5));

    strategy.on_fill(50);
    CHECK(strategy.fill_progress() == Catch::Approx(1.0));
    CHECK(strategy.is_complete());
}

TEST_CASE("VWAP: auto-initializes from order via route()", "[routing][vwap]")
{
    VWAPStrategy strategy;

    auto order = make_order(Side::Buy, 1000, 155.0);
    auto nbbo = make_nbbo(149.0, 500, 1, 150.0, 500, 1);

    AggregatedBook book{};
    book.symbol = "AAPL";
    book.nbbo = nbbo;
    add_ask_level(book, 150.0, 1, 500);

    std::vector<VenueScore> venues = {make_venue_score(1)};

    // Call route() without calling begin() first.
    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    CHECK_FALSE(strategy.is_complete());
}

// ============================================================================
// Strategy type and name
// ============================================================================

TEST_CASE("Strategy type and name accessors", "[routing]")
{
    BestPriceStrategy bp;
    CHECK(bp.type() == sor::RoutingStrategy::BestPrice);
    CHECK(std::string(bp.name()) == "BestPrice");

    LiquiditySweepStrategy ls;
    CHECK(ls.type() == sor::RoutingStrategy::LiquiditySweep);
    CHECK(std::string(ls.name()) == "LiquiditySweep");

    SmartIOCStrategy si;
    CHECK(si.type() == sor::RoutingStrategy::SmartIOC);
    CHECK(std::string(si.name()) == "SmartIOC");

    VWAPStrategy vw;
    CHECK(vw.type() == sor::RoutingStrategy::VWAP);
    CHECK(std::string(vw.name()) == "VWAP");
}
