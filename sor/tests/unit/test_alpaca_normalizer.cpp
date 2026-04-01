// Unit tests for AlpacaNormalizer: JSON parsing, price/quantity conversion.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "market_data/alpaca/alpaca_normalizer.h"
#include "core/types.h"

using namespace sor;
using namespace sor::market_data;
using namespace sor::market_data::alpaca;

// ---------------------------------------------------------------------------
// Quote parsing
// ---------------------------------------------------------------------------

TEST_CASE("AlpacaNormalizer: parse single quote message", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;
    VenueId received_venue = 0;
    Symbol received_symbol;
    OrderBook received_book{};

    auto callback = [&](VenueId vid, const Symbol &sym, const OrderBook &book) {
        ++callback_count;
        received_venue = vid;
        received_symbol = sym;
        received_book = book;
    };

    std::string json = R"([{"T":"q","S":"AAPL","bp":150.25,"bs":200,"ap":150.30,"as":100,"bx":"Q","ax":"N","t":"2024-01-15T10:30:00Z","c":["R"]}])";

    normalizer.on_message(json, callback);

    REQUIRE(callback_count == 1);
    REQUIRE(received_venue == 100);
    REQUIRE(received_symbol == Symbol("AAPL"));
    REQUIRE(received_book.bids.depth == 1);
    REQUIRE(received_book.asks.depth == 1);
    REQUIRE(received_book.bids.levels[0].price == to_price(150.25));
    REQUIRE(received_book.bids.levels[0].quantity == 200);
    REQUIRE(received_book.asks.levels[0].price == to_price(150.30));
    REQUIRE(received_book.asks.levels[0].quantity == 100);
}

TEST_CASE("AlpacaNormalizer: parse multiple quotes in one message", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    std::string json = R"([
        {"T":"q","S":"AAPL","bp":150.25,"bs":200,"ap":150.30,"as":100},
        {"T":"q","S":"MSFT","bp":380.00,"bs":50,"ap":380.10,"as":75}
    ])";

    normalizer.on_message(json, callback);

    REQUIRE(callback_count == 2);
    REQUIRE(normalizer.symbol_count() == 2);

    const OrderBook *aapl = normalizer.get_book(Symbol("AAPL"));
    REQUIRE(aapl != nullptr);
    REQUIRE(aapl->bids.levels[0].price == to_price(150.25));

    const OrderBook *msft = normalizer.get_book(Symbol("MSFT"));
    REQUIRE(msft != nullptr);
    REQUIRE(msft->bids.levels[0].price == to_price(380.00));
}

TEST_CASE("AlpacaNormalizer: trade messages do not generate book callbacks", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    std::string json = R"([{"T":"t","S":"AAPL","p":150.25,"s":100,"t":"2024-01-15T10:30:00Z","x":"Q","i":12345,"c":["@"]}])";

    normalizer.on_message(json, callback);

    REQUIRE(callback_count == 0);
}

TEST_CASE("AlpacaNormalizer: control messages are ignored", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    // Auth success message
    normalizer.on_message(R"([{"T":"success","msg":"authenticated"}])", callback);
    REQUIRE(callback_count == 0);

    // Subscription confirmation
    normalizer.on_message(R"([{"T":"subscription","quotes":["AAPL"],"trades":[]}])", callback);
    REQUIRE(callback_count == 0);

    // Error message
    normalizer.on_message(R"([{"T":"error","code":401,"msg":"not authenticated"}])", callback);
    REQUIRE(callback_count == 0);
}

TEST_CASE("AlpacaNormalizer: malformed JSON is handled gracefully", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    // Invalid JSON
    normalizer.on_message("{not valid json", callback);
    REQUIRE(callback_count == 0);

    // Empty string
    normalizer.on_message("", callback);
    REQUIRE(callback_count == 0);

    // Non-array JSON
    normalizer.on_message(R"({"T":"q","S":"AAPL"})", callback);
    REQUIRE(callback_count == 0);
}

TEST_CASE("AlpacaNormalizer: skip quotes with missing fields", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    // Missing bp
    normalizer.on_message(R"([{"T":"q","S":"AAPL","bs":200,"ap":150.30,"as":100}])", callback);
    REQUIRE(callback_count == 0);

    // Missing S
    normalizer.on_message(R"([{"T":"q","bp":150.25,"bs":200,"ap":150.30,"as":100}])", callback);
    REQUIRE(callback_count == 0);
}

TEST_CASE("AlpacaNormalizer: skip quotes with invalid values", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    int callback_count = 0;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &) {
        ++callback_count;
    };

    // Zero bid price
    normalizer.on_message(R"([{"T":"q","S":"AAPL","bp":0.0,"bs":200,"ap":150.30,"as":100}])", callback);
    REQUIRE(callback_count == 0);

    // Negative ask size
    normalizer.on_message(R"([{"T":"q","S":"AAPL","bp":150.25,"bs":200,"ap":150.30,"as":-1}])", callback);
    REQUIRE(callback_count == 0);
}

TEST_CASE("AlpacaNormalizer: price conversion accuracy", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    OrderBook received_book{};

    auto callback = [&](VenueId, const Symbol &, const OrderBook &book) {
        received_book = book;
    };

    normalizer.on_message(
        R"([{"T":"q","S":"AAPL","bp":150.05,"bs":100,"ap":150.10,"as":50}])", callback);

    // Verify round-trip accuracy
    REQUIRE(to_double(received_book.bids.levels[0].price) == Catch::Approx(150.05).epsilon(0.0001));
    REQUIRE(to_double(received_book.asks.levels[0].price) == Catch::Approx(150.10).epsilon(0.0001));
}

TEST_CASE("AlpacaNormalizer: sequence numbers increment", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);
    std::vector<uint64_t> sequences;

    auto callback = [&](VenueId, const Symbol &, const OrderBook &book) {
        sequences.push_back(book.sequence);
    };

    normalizer.on_message(
        R"([{"T":"q","S":"AAPL","bp":150.25,"bs":200,"ap":150.30,"as":100}])", callback);
    normalizer.on_message(
        R"([{"T":"q","S":"AAPL","bp":150.26,"bs":150,"ap":150.31,"as":80}])", callback);

    REQUIRE(sequences.size() == 2);
    REQUIRE(sequences[1] > sequences[0]);
}

TEST_CASE("AlpacaNormalizer: book updates overwrite previous state", "[market_data][alpaca]")
{
    AlpacaNormalizer normalizer(100);

    auto noop = [](VenueId, const Symbol &, const OrderBook &) {};

    normalizer.on_message(
        R"([{"T":"q","S":"AAPL","bp":150.25,"bs":200,"ap":150.30,"as":100}])", noop);

    const OrderBook *book1 = normalizer.get_book(Symbol("AAPL"));
    REQUIRE(book1 != nullptr);
    REQUIRE(book1->bids.levels[0].quantity == 200);

    normalizer.on_message(
        R"([{"T":"q","S":"AAPL","bp":150.50,"bs":300,"ap":150.55,"as":150}])", noop);

    const OrderBook *book2 = normalizer.get_book(Symbol("AAPL"));
    REQUIRE(book2 != nullptr);
    REQUIRE(book2->bids.levels[0].price == to_price(150.50));
    REQUIRE(book2->bids.levels[0].quantity == 300);
    REQUIRE(book2->bids.depth == 1); // Still single-level
}
