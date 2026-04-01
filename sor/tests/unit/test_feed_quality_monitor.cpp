// Unit tests for FeedQualityMonitor: staleness detection, counters.

#include <catch2/catch_test_macros.hpp>

#include "market_data/feed_quality_monitor.h"

#include <chrono>
#include <thread>

using namespace sor::market_data;

TEST_CASE("FeedQualityMonitor: fresh monitor is stale (no messages)", "[market_data][quality]")
{
    FeedQualityMonitor monitor(std::chrono::seconds(5));
    REQUIRE(monitor.is_stale());

    auto stats = monitor.get_stats();
    REQUIRE(stats.messages_received == 0);
    REQUIRE(stats.quotes_processed == 0);
}

TEST_CASE("FeedQualityMonitor: not stale after receiving message", "[market_data][quality]")
{
    FeedQualityMonitor monitor(std::chrono::seconds(5));
    monitor.on_message_received();
    REQUIRE_FALSE(monitor.is_stale());
}

TEST_CASE("FeedQualityMonitor: becomes stale after threshold", "[market_data][quality]")
{
    // Use a very short threshold for testing
    FeedQualityMonitor monitor(std::chrono::milliseconds(50));
    monitor.on_message_received();
    REQUIRE_FALSE(monitor.is_stale());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(monitor.is_stale());
}

TEST_CASE("FeedQualityMonitor: counters increment correctly", "[market_data][quality]")
{
    FeedQualityMonitor monitor(std::chrono::seconds(5));

    monitor.on_message_received();
    monitor.on_message_received();
    monitor.on_message_received();
    monitor.on_quote_processed();
    monitor.on_quote_processed();
    monitor.on_trade_processed();
    monitor.on_parse_error();
    monitor.on_reconnection();
    monitor.on_sequence_gap();
    monitor.on_sequence_gap();

    auto stats = monitor.get_stats();
    REQUIRE(stats.messages_received == 3);
    REQUIRE(stats.quotes_processed == 2);
    REQUIRE(stats.trades_processed == 1);
    REQUIRE(stats.parse_errors == 1);
    REQUIRE(stats.reconnections == 1);
    REQUIRE(stats.sequence_gaps == 2);
}

TEST_CASE("FeedQualityMonitor: reset clears all counters", "[market_data][quality]")
{
    FeedQualityMonitor monitor(std::chrono::seconds(5));

    monitor.on_message_received();
    monitor.on_quote_processed();
    monitor.on_parse_error();

    monitor.reset();

    auto stats = monitor.get_stats();
    REQUIRE(stats.messages_received == 0);
    REQUIRE(stats.quotes_processed == 0);
    REQUIRE(stats.parse_errors == 0);
    REQUIRE(monitor.is_stale()); // Back to stale after reset
}

TEST_CASE("FeedQualityMonitor: last_message_time is set after message", "[market_data][quality]")
{
    FeedQualityMonitor monitor(std::chrono::seconds(5));

    auto before = std::chrono::steady_clock::now();
    monitor.on_message_received();
    auto after = std::chrono::steady_clock::now();

    auto stats = monitor.get_stats();
    REQUIRE(stats.last_message_time >= before);
    REQUIRE(stats.last_message_time <= after);
}
