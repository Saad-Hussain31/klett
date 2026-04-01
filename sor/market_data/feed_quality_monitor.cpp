#include "market_data/feed_quality_monitor.h"

namespace sor::market_data
{

    FeedQualityMonitor::FeedQualityMonitor(std::chrono::milliseconds staleness_threshold)
        : staleness_threshold_(staleness_threshold)
    {
    }

    void FeedQualityMonitor::on_message_received()
    {
        messages_received_.fetch_add(1, std::memory_order_relaxed);
        auto now = std::chrono::steady_clock::now();
        last_message_ns_.store(
            now.time_since_epoch().count(), std::memory_order_release);
    }

    void FeedQualityMonitor::on_quote_processed()
    {
        quotes_processed_.fetch_add(1, std::memory_order_relaxed);
    }

    void FeedQualityMonitor::on_trade_processed()
    {
        trades_processed_.fetch_add(1, std::memory_order_relaxed);
    }

    void FeedQualityMonitor::on_parse_error()
    {
        parse_errors_.fetch_add(1, std::memory_order_relaxed);
    }

    void FeedQualityMonitor::on_reconnection()
    {
        reconnections_.fetch_add(1, std::memory_order_relaxed);
    }

    void FeedQualityMonitor::on_sequence_gap()
    {
        sequence_gaps_.fetch_add(1, std::memory_order_relaxed);
    }

    bool FeedQualityMonitor::is_stale() const
    {
        auto last_ns = last_message_ns_.load(std::memory_order_acquire);
        if (last_ns == 0)
            return true; // No messages received yet

        auto last_tp = std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(last_ns));
        auto elapsed = std::chrono::steady_clock::now() - last_tp;
        return elapsed > staleness_threshold_;
    }

    FeedQualityStats FeedQualityMonitor::get_stats() const
    {
        FeedQualityStats stats;
        stats.messages_received = messages_received_.load(std::memory_order_relaxed);
        stats.quotes_processed = quotes_processed_.load(std::memory_order_relaxed);
        stats.trades_processed = trades_processed_.load(std::memory_order_relaxed);
        stats.parse_errors = parse_errors_.load(std::memory_order_relaxed);
        stats.sequence_gaps = sequence_gaps_.load(std::memory_order_relaxed);
        stats.reconnections = reconnections_.load(std::memory_order_relaxed);

        auto last_ns = last_message_ns_.load(std::memory_order_acquire);
        if (last_ns != 0)
        {
            stats.last_message_time = std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(last_ns));
        }

        return stats;
    }

    void FeedQualityMonitor::reset()
    {
        messages_received_.store(0, std::memory_order_relaxed);
        quotes_processed_.store(0, std::memory_order_relaxed);
        trades_processed_.store(0, std::memory_order_relaxed);
        parse_errors_.store(0, std::memory_order_relaxed);
        sequence_gaps_.store(0, std::memory_order_relaxed);
        reconnections_.store(0, std::memory_order_relaxed);
        last_message_ns_.store(0, std::memory_order_relaxed);
    }

} // namespace sor::market_data
