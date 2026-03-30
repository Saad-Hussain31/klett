// Unit tests for OrderBook, BookSide, and MarketDataAggregator.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "market_data/book.h"
#include "market_data/aggregator.h"
#include "core/types.h"

#include <chrono>
#include <thread>

using namespace sor;
using namespace sor::market_data;

// ---------------------------------------------------------------------------
// OrderBook / BookSide basics
// ---------------------------------------------------------------------------

TEST_CASE("OrderBook: empty book has invalid best bid/ask", "[market_data][book]")
{
    OrderBook book{};
    book.symbol = "AAPL";
    REQUIRE(book.best_bid() == INVALID_PRICE);
    REQUIRE(book.best_ask() == INVALID_PRICE);
    REQUIRE(book.bids.empty());
    REQUIRE(book.asks.empty());
}

TEST_CASE("OrderBook: add bid levels, best_bid is highest", "[market_data][book]")
{
    OrderBook book{};
    book.symbol = "AAPL";

    // Insert bids in sorted order (descending).
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.bids.insert_sorted({to_price(149.5), 200, 3}, true);
    book.bids.insert_sorted({to_price(151.0), 50, 2}, true);

    REQUIRE(book.bids.depth == 3);
    // Best bid should be 151.0 (highest).
    CHECK(book.best_bid() == to_price(151.0));
    CHECK(book.bids.levels[0].quantity == 50);

    // Second level should be 150.0.
    CHECK(book.bids.levels[1].price == to_price(150.0));
}

TEST_CASE("OrderBook: add ask levels, best_ask is lowest", "[market_data][book]")
{
    OrderBook book{};
    book.symbol = "AAPL";

    // Insert asks in sorted order (ascending).
    book.asks.insert_sorted({to_price(152.0), 100, 5}, false);
    book.asks.insert_sorted({to_price(151.0), 200, 3}, false);
    book.asks.insert_sorted({to_price(153.0), 50, 2}, false);

    REQUIRE(book.asks.depth == 3);
    // Best ask should be 151.0 (lowest).
    CHECK(book.best_ask() == to_price(151.0));
    CHECK(book.asks.levels[0].quantity == 200);
}

TEST_CASE("OrderBook: update existing bid level", "[market_data][book]")
{
    OrderBook book{};
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    REQUIRE(book.bids.depth == 1);
    CHECK(book.bids.levels[0].quantity == 100);

    // Update quantity at the same price.
    book.bids.update(to_price(150.0), 200, 10);
    REQUIRE(book.bids.depth == 1);
    CHECK(book.bids.levels[0].quantity == 200);
    CHECK(book.bids.levels[0].order_count == 10);
}

TEST_CASE("OrderBook: remove level with qty=0", "[market_data][book]")
{
    OrderBook book{};
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.bids.insert_sorted({to_price(149.0), 200, 3}, true);
    REQUIRE(book.bids.depth == 2);

    // Remove level at 150.0 by setting qty to 0.
    book.bids.update(to_price(150.0), 0, 0);
    REQUIRE(book.bids.depth == 1);
    CHECK(book.best_bid() == to_price(149.0));
}

TEST_CASE("OrderBook: remove non-existent level is no-op", "[market_data][book]")
{
    OrderBook book{};
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    REQUIRE(book.bids.depth == 1);

    book.bids.remove(to_price(999.0));
    REQUIRE(book.bids.depth == 1); // unchanged
}

// ---------------------------------------------------------------------------
// Crossed book detection
// ---------------------------------------------------------------------------

TEST_CASE("OrderBook: crossed book detection", "[market_data][book]")
{
    OrderBook book{};

    SECTION("Normal book is not crossed")
    {
        book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
        book.asks.insert_sorted({to_price(151.0), 100, 5}, false);
        REQUIRE_FALSE(book.is_crossed());
    }

    SECTION("Crossed book: bid >= ask")
    {
        book.bids.insert_sorted({to_price(152.0), 100, 5}, true);
        book.asks.insert_sorted({to_price(151.0), 100, 5}, false);
        REQUIRE(book.is_crossed());
    }

    SECTION("Exact touch: bid == ask is considered crossed")
    {
        book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
        book.asks.insert_sorted({to_price(150.0), 100, 5}, false);
        REQUIRE(book.is_crossed());
    }

    SECTION("Empty book is not crossed")
    {
        REQUIRE_FALSE(book.is_crossed());
    }

    SECTION("One-sided book is not crossed")
    {
        book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
        REQUIRE_FALSE(book.is_crossed());
    }
}

// ---------------------------------------------------------------------------
// Mid price and spread
// ---------------------------------------------------------------------------

TEST_CASE("OrderBook: mid_price calculation", "[market_data][book]")
{
    OrderBook book{};

    SECTION("Normal book")
    {
        book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
        book.asks.insert_sorted({to_price(152.0), 100, 5}, false);
        Price mid = book.mid_price();
        // mid = (150 + 152) / 2 = 151
        REQUIRE(mid == (to_price(150.0) + to_price(152.0)) / 2);
    }

    SECTION("Empty book returns INVALID_PRICE")
    {
        REQUIRE(book.mid_price() == INVALID_PRICE);
    }
}

TEST_CASE("OrderBook: spread calculation", "[market_data][book]")
{
    OrderBook book{};

    SECTION("Normal spread")
    {
        book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
        book.asks.insert_sorted({to_price(151.0), 100, 5}, false);
        Price sp = book.spread();
        REQUIRE(sp == to_price(151.0) - to_price(150.0));
        REQUIRE(to_double(sp) == Catch::Approx(1.0));
    }

    SECTION("Tight spread")
    {
        book.bids.insert_sorted({to_price(150.00), 100, 5}, true);
        book.asks.insert_sorted({to_price(150.01), 100, 5}, false);
        Price sp = book.spread();
        REQUIRE(to_double(sp) == Catch::Approx(0.01).epsilon(1e-7));
    }

    SECTION("Empty book returns INVALID_PRICE")
    {
        REQUIRE(book.spread() == INVALID_PRICE);
    }
}

// ---------------------------------------------------------------------------
// quantity_at_or_better
// ---------------------------------------------------------------------------

TEST_CASE("OrderBook: quantity_at_or_better", "[market_data][book]")
{
    OrderBook book{};

    // Bids: 151@100, 150@200, 149@300 (descending).
    book.bids.insert_sorted({to_price(151.0), 100, 1}, true);
    book.bids.insert_sorted({to_price(150.0), 200, 1}, true);
    book.bids.insert_sorted({to_price(149.0), 300, 1}, true);

    // Asks: 152@100, 153@200, 154@300 (ascending).
    book.asks.insert_sorted({to_price(152.0), 100, 1}, false);
    book.asks.insert_sorted({to_price(153.0), 200, 1}, false);
    book.asks.insert_sorted({to_price(154.0), 300, 1}, false);

    SECTION("Buy side: bids at or better than 150.0")
    {
        Quantity q = book.quantity_at_or_better(Side::Buy, to_price(150.0));
        // 151@100 + 150@200 = 300
        CHECK(q == 300);
    }

    SECTION("Sell side: asks at or better than 153.0")
    {
        Quantity q = book.quantity_at_or_better(Side::Sell, to_price(153.0));
        // 152@100 + 153@200 = 300
        CHECK(q == 300);
    }
}

// ---------------------------------------------------------------------------
// BookSide::clear
// ---------------------------------------------------------------------------

TEST_CASE("OrderBook: clear resets everything", "[market_data][book]")
{
    OrderBook book{};
    book.symbol = "AAPL";
    book.sequence = 42;

    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.asks.insert_sorted({to_price(151.0), 100, 5}, false);

    book.clear();

    CHECK(book.bids.depth == 0);
    CHECK(book.asks.depth == 0);
    CHECK(book.sequence == 0);
    CHECK(book.best_bid() == INVALID_PRICE);
    CHECK(book.best_ask() == INVALID_PRICE);
}

// ============================================================================
// MarketDataAggregator
// ============================================================================

TEST_CASE("Aggregator: register venues", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);
    agg.register_venue(2);

    // Duplicate registration should be harmless.
    agg.register_venue(1);

    // NBBO for unknown symbol should be default (invalid).
    NBBO nbbo = agg.get_nbbo(Symbol("AAPL"));
    CHECK(nbbo.best_bid == INVALID_PRICE);
    CHECK(nbbo.best_ask == INVALID_PRICE);
}

TEST_CASE("Aggregator: single venue book update and NBBO", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);

    OrderBook book{};
    book.symbol = "AAPL";
    book.venue_id = 1;
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.asks.insert_sorted({to_price(151.0), 200, 3}, false);
    book.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book);

    NBBO nbbo = agg.get_nbbo(Symbol("AAPL"));
    CHECK(nbbo.best_bid == to_price(150.0));
    CHECK(nbbo.best_bid_qty == 100);
    CHECK(nbbo.best_bid_venue == 1);
    CHECK(nbbo.best_ask == to_price(151.0));
    CHECK(nbbo.best_ask_qty == 200);
    CHECK(nbbo.best_ask_venue == 1);
    CHECK(nbbo.valid());
}

TEST_CASE("Aggregator: NBBO across two venues", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);
    agg.register_venue(2);

    // Venue 1: bid 150.0, ask 152.0
    OrderBook book1{};
    book1.symbol = "AAPL";
    book1.venue_id = 1;
    book1.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book1.asks.insert_sorted({to_price(152.0), 100, 3}, false);
    book1.last_update = std::chrono::steady_clock::now();

    // Venue 2: bid 150.5, ask 151.0
    OrderBook book2{};
    book2.symbol = "AAPL";
    book2.venue_id = 2;
    book2.bids.insert_sorted({to_price(150.5), 200, 3}, true);
    book2.asks.insert_sorted({to_price(151.0), 150, 4}, false);
    book2.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book1);
    agg.on_book_update(2, Symbol("AAPL"), book2);

    NBBO nbbo = agg.get_nbbo(Symbol("AAPL"));
    // Best bid = 150.5 from venue 2 (highest).
    CHECK(nbbo.best_bid == to_price(150.5));
    CHECK(nbbo.best_bid_venue == 2);
    CHECK(nbbo.best_bid_qty == 200);

    // Best ask = 151.0 from venue 2 (lowest).
    CHECK(nbbo.best_ask == to_price(151.0));
    CHECK(nbbo.best_ask_venue == 2);
    CHECK(nbbo.best_ask_qty == 150);

    CHECK(nbbo.valid());
}

TEST_CASE("Aggregator: best bid from one venue, best ask from another", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);
    agg.register_venue(2);

    // Venue 1: highest bid.
    OrderBook book1{};
    book1.symbol = "AAPL";
    book1.venue_id = 1;
    book1.bids.insert_sorted({to_price(151.0), 100, 5}, true);
    book1.asks.insert_sorted({to_price(153.0), 100, 3}, false);
    book1.last_update = std::chrono::steady_clock::now();

    // Venue 2: lowest ask.
    OrderBook book2{};
    book2.symbol = "AAPL";
    book2.venue_id = 2;
    book2.bids.insert_sorted({to_price(149.0), 100, 3}, true);
    book2.asks.insert_sorted({to_price(152.0), 200, 4}, false);
    book2.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book1);
    agg.on_book_update(2, Symbol("AAPL"), book2);

    NBBO nbbo = agg.get_nbbo(Symbol("AAPL"));
    // Best bid from venue 1 (151.0), best ask from venue 2 (152.0).
    CHECK(nbbo.best_bid == to_price(151.0));
    CHECK(nbbo.best_bid_venue == 1);
    CHECK(nbbo.best_ask == to_price(152.0));
    CHECK(nbbo.best_ask_venue == 2);
    CHECK(nbbo.valid());
}

TEST_CASE("Aggregator: same price at both venues accumulates quantity", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);
    agg.register_venue(2);

    OrderBook book1{};
    book1.symbol = "AAPL";
    book1.venue_id = 1;
    book1.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book1.asks.insert_sorted({to_price(151.0), 100, 3}, false);
    book1.last_update = std::chrono::steady_clock::now();

    OrderBook book2{};
    book2.symbol = "AAPL";
    book2.venue_id = 2;
    book2.bids.insert_sorted({to_price(150.0), 200, 3}, true);
    book2.asks.insert_sorted({to_price(151.0), 150, 4}, false);
    book2.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book1);
    agg.on_book_update(2, Symbol("AAPL"), book2);

    NBBO nbbo = agg.get_nbbo(Symbol("AAPL"));
    // Same best bid price -> qty accumulated.
    CHECK(nbbo.best_bid == to_price(150.0));
    CHECK(nbbo.best_bid_qty == 300); // 100 + 200

    CHECK(nbbo.best_ask == to_price(151.0));
    CHECK(nbbo.best_ask_qty == 250); // 100 + 150
}

// ---------------------------------------------------------------------------
// Aggregated book
// ---------------------------------------------------------------------------

TEST_CASE("Aggregator: get_aggregated_book merges venues", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);
    agg.register_venue(2);

    OrderBook book1{};
    book1.symbol = "AAPL";
    book1.venue_id = 1;
    book1.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book1.asks.insert_sorted({to_price(151.0), 100, 3}, false);
    book1.last_update = std::chrono::steady_clock::now();

    OrderBook book2{};
    book2.symbol = "AAPL";
    book2.venue_id = 2;
    book2.bids.insert_sorted({to_price(150.5), 200, 3}, true);
    book2.asks.insert_sorted({to_price(151.0), 150, 4}, false);
    book2.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book1);
    agg.on_book_update(2, Symbol("AAPL"), book2);

    auto agg_book = agg.get_aggregated_book(Symbol("AAPL"));
    CHECK(agg_book.bid_depth >= 2); // two distinct bid prices
    CHECK(agg_book.ask_depth >= 1); // same ask price, merged

    // Best bid should be 150.5 from venue 2.
    CHECK(agg_book.bids[0].price == to_price(150.5));
}

// ---------------------------------------------------------------------------
// Staleness detection
// ---------------------------------------------------------------------------

TEST_CASE("Aggregator: staleness detection with no data is stale", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);

    // No book updates at all -- considered stale.
    CHECK(agg.is_stale(Symbol("AAPL"), std::chrono::microseconds(1'000'000)));
}

TEST_CASE("Aggregator: fresh data is not stale", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);

    OrderBook book{};
    book.symbol = "AAPL";
    book.venue_id = 1;
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.asks.insert_sorted({to_price(151.0), 100, 5}, false);
    book.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book);

    // With a generous max_age, should not be stale.
    CHECK_FALSE(agg.is_stale(Symbol("AAPL"), std::chrono::seconds(10)));
}

// ---------------------------------------------------------------------------
// NBBO callback
// ---------------------------------------------------------------------------

TEST_CASE("Aggregator: NBBO callback fires on update", "[market_data][aggregator]")
{
    MarketDataAggregator agg;
    agg.register_venue(1);

    int callback_count = 0;
    NBBO captured_nbbo{};

    agg.set_nbbo_callback([&](const Symbol &sym, const NBBO &nbbo)
                          {
        ++callback_count;
        captured_nbbo = nbbo; });

    OrderBook book{};
    book.symbol = "AAPL";
    book.venue_id = 1;
    book.bids.insert_sorted({to_price(150.0), 100, 5}, true);
    book.asks.insert_sorted({to_price(151.0), 100, 5}, false);
    book.last_update = std::chrono::steady_clock::now();

    agg.on_book_update(1, Symbol("AAPL"), book);

    CHECK(callback_count >= 1);
    CHECK(captured_nbbo.best_bid == to_price(150.0));
    CHECK(captured_nbbo.best_ask == to_price(151.0));
}

// ---------------------------------------------------------------------------
// NBBO validity
// ---------------------------------------------------------------------------

TEST_CASE("NBBO: valid() requires both sides", "[market_data]")
{
    NBBO nbbo{};
    CHECK_FALSE(nbbo.valid()); // default -- invalid

    nbbo.best_bid = to_price(100.0);
    nbbo.best_bid_qty = 100;
    CHECK_FALSE(nbbo.valid()); // still no ask

    nbbo.best_ask = to_price(101.0);
    nbbo.best_ask_qty = 100;
    CHECK(nbbo.valid());

    // Crossed NBBO is not valid (bid >= ask).
    nbbo.best_bid = to_price(102.0);
    CHECK_FALSE(nbbo.valid());
}

TEST_CASE("NBBO: spread and mid_price", "[market_data]")
{
    NBBO nbbo{};
    nbbo.best_bid = to_price(100.0);
    nbbo.best_ask = to_price(102.0);
    nbbo.best_bid_qty = 100;
    nbbo.best_ask_qty = 100;

    CHECK(nbbo.spread() == to_price(102.0) - to_price(100.0));
    CHECK(to_double(nbbo.spread()) == Catch::Approx(2.0));
    CHECK(to_double(nbbo.mid_price()) == Catch::Approx(101.0));
}
