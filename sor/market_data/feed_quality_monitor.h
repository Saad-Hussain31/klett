#pragma once

// Feed quality monitoring: staleness detection, gap tracking, reconnection counting.
// All counters are atomic for lock-free updates from I/O threads.

#include <atomic>
#include <chrono>
#include <cstdint>

namespace sor::market_data
{

    struct FeedQualityStats
    {
        uint64_t messages_received{0};
        uint64_t quotes_processed{0};
        uint64_t trades_processed{0};
        uint64_t parse_errors{0};
        uint64_t sequence_gaps{0};
        uint64_t reconnections{0};
        std::chrono::steady_clock::time_point last_message_time{};
    };

    class FeedQualityMonitor
    {
    public:
        explicit FeedQualityMonitor(
            std::chrono::milliseconds staleness_threshold = std::chrono::seconds(5));

        void on_message_received();
        void on_quote_processed();
        void on_trade_processed();
        void on_parse_error();
        void on_reconnection();
        void on_sequence_gap();

        [[nodiscard]] bool is_stale() const;
        [[nodiscard]] FeedQualityStats get_stats() const;
        void reset();

    private:
        std::chrono::milliseconds staleness_threshold_;

        std::atomic<uint64_t> messages_received_{0};
        std::atomic<uint64_t> quotes_processed_{0};
        std::atomic<uint64_t> trades_processed_{0};
        std::atomic<uint64_t> parse_errors_{0};
        std::atomic<uint64_t> sequence_gaps_{0};
        std::atomic<uint64_t> reconnections_{0};

        // Stored as nanoseconds since epoch for atomic access
        std::atomic<int64_t> last_message_ns_{0};
    };

} // namespace sor::market_data
