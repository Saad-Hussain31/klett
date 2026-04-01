// Thread-safety tests for MarketDataAggregator with shared_mutex.

#include <catch2/catch_test_macros.hpp>

#include "market_data/aggregator.h"
#include "core/types.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace sor;
using namespace sor::market_data;

namespace
{
    OrderBook make_book(VenueId venue_id, const Symbol &symbol,
                        double bid_price, Quantity bid_qty,
                        double ask_price, Quantity ask_qty)
    {
        OrderBook book{};
        book.symbol = symbol;
        book.venue_id = venue_id;
        book.sequence = 1;
        book.last_update = std::chrono::steady_clock::now();

        PriceLevel bid;
        bid.price = to_price(bid_price);
        bid.quantity = bid_qty;
        bid.order_count = 1;
        book.bids.levels[0] = bid;
        book.bids.depth = 1;

        PriceLevel ask;
        ask.price = to_price(ask_price);
        ask.quantity = ask_qty;
        ask.order_count = 1;
        book.asks.levels[0] = ask;
        book.asks.depth = 1;

        return book;
    }
} // anonymous namespace

TEST_CASE("MarketDataAggregator: concurrent writers and readers", "[market_data][threading]")
{
    MarketDataAggregator aggregator;

    constexpr int NUM_VENUES = 4;
    constexpr int NUM_READERS = 4;
    constexpr int ITERATIONS = 1000;

    Symbol symbol("AAPL");

    for (VenueId v = 1; v <= NUM_VENUES; ++v)
        aggregator.register_venue(v);

    // Pre-populate with initial data so readers have something to read
    for (VenueId v = 1; v <= NUM_VENUES; ++v)
    {
        auto book = make_book(v, symbol, 150.0, 100, 150.10, 100);
        aggregator.on_book_update(v, symbol, book);
    }

    std::atomic<bool> go{false};
    std::atomic<int> writer_done{0};
    std::atomic<int> reader_done{0};
    std::atomic<int> read_count{0};

    // Writer threads: each updates a different venue
    std::vector<std::thread> writers;
    for (VenueId v = 1; v <= NUM_VENUES; ++v)
    {
        writers.emplace_back([&, v]() {
            while (!go.load())
                std::this_thread::yield();

            for (int i = 0; i < ITERATIONS; ++i)
            {
                double offset = static_cast<double>(i) * 0.01;
                auto book = make_book(v, symbol, 150.0 + offset, 100 + i,
                                      150.10 + offset, 100 + i);
                aggregator.on_book_update(v, symbol, book);
            }
            writer_done.fetch_add(1);
        });
    }

    // Reader threads: continuously read NBBO
    std::vector<std::thread> readers;
    for (int r = 0; r < NUM_READERS; ++r)
    {
        readers.emplace_back([&]() {
            while (!go.load())
                std::this_thread::yield();

            while (writer_done.load() < NUM_VENUES)
            {
                auto nbbo = aggregator.get_nbbo(symbol);
                // Verify structural integrity: no corrupted/partial reads
                if (nbbo.best_bid != INVALID_PRICE)
                {
                    REQUIRE(nbbo.best_bid_qty > 0);
                    REQUIRE(nbbo.best_bid_venue > 0);
                }
                if (nbbo.best_ask != INVALID_PRICE)
                {
                    REQUIRE(nbbo.best_ask_qty > 0);
                    REQUIRE(nbbo.best_ask_venue > 0);
                }
                read_count.fetch_add(1);
            }
            reader_done.fetch_add(1);
        });
    }

    // Start all threads simultaneously
    go.store(true);

    for (auto &w : writers)
        w.join();
    for (auto &r : readers)
        r.join();

    REQUIRE(writer_done.load() == NUM_VENUES);
    REQUIRE(reader_done.load() == NUM_READERS);
    REQUIRE(read_count.load() > 0);

    // Final NBBO should be valid
    auto final_nbbo = aggregator.get_nbbo(symbol);
    REQUIRE(final_nbbo.best_bid != INVALID_PRICE);
    REQUIRE(final_nbbo.best_ask != INVALID_PRICE);
}

TEST_CASE("MarketDataAggregator: concurrent register_venue and book_update", "[market_data][threading]")
{
    MarketDataAggregator aggregator;
    Symbol symbol("MSFT");

    std::atomic<bool> go{false};

    // Thread 1: registers venues
    std::thread registrar([&]() {
        while (!go.load())
            std::this_thread::yield();
        for (VenueId v = 1; v <= 10; ++v)
        {
            aggregator.register_venue(v);
            std::this_thread::yield();
        }
    });

    // Thread 2: sends book updates (some venues may not be registered yet)
    std::thread updater([&]() {
        while (!go.load())
            std::this_thread::yield();
        for (int i = 0; i < 100; ++i)
        {
            VenueId v = static_cast<VenueId>((i % 10) + 1);
            auto book = make_book(v, symbol, 380.0, 50, 380.10, 50);
            aggregator.on_book_update(v, symbol, book);
            std::this_thread::yield();
        }
    });

    go.store(true);
    registrar.join();
    updater.join();

    // Should not crash or produce corrupt state
    auto nbbo = aggregator.get_nbbo(symbol);
    // NBBO might or might not be valid depending on timing, but should not crash
    (void)nbbo;
}

TEST_CASE("MarketDataAggregator: NBBO callback fires under lock safely", "[market_data][threading]")
{
    MarketDataAggregator aggregator;
    Symbol symbol("GOOGL");
    aggregator.register_venue(1);
    aggregator.register_venue(2);

    std::atomic<int> callback_count{0};

    aggregator.set_nbbo_callback(
        [&callback_count](const Symbol &, const NBBO &) {
            callback_count.fetch_add(1);
        });

    std::atomic<bool> go{false};

    std::thread t1([&]() {
        while (!go.load())
            std::this_thread::yield();
        for (int i = 0; i < 500; ++i)
        {
            auto book = make_book(1, symbol, 100.0 + i * 0.01, 100,
                                  100.10 + i * 0.01, 100);
            aggregator.on_book_update(1, symbol, book);
        }
    });

    std::thread t2([&]() {
        while (!go.load())
            std::this_thread::yield();
        for (int i = 0; i < 500; ++i)
        {
            auto book = make_book(2, symbol, 100.0 + i * 0.01, 150,
                                  100.10 + i * 0.01, 150);
            aggregator.on_book_update(2, symbol, book);
        }
    });

    go.store(true);
    t1.join();
    t2.join();

    // Callback should have been invoked at least once
    REQUIRE(callback_count.load() > 0);
}
