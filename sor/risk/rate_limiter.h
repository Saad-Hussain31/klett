#pragma once

// Lock-free sliding-window rate limiter.
// Uses a simple per-second window with atomic operations -- zero contention
// on the fast path when the window has not rolled over.

#include <atomic>
#include <chrono>
#include <cstdint>

namespace sor::risk
{

    class RateLimiter
    {
    public:
        explicit RateLimiter(int32_t max_per_second) noexcept;

        // Returns true if the request is allowed, false if rate exceeded.
        // Thread-safe (lock-free).
        [[nodiscard]] bool try_acquire() noexcept;

        // Reset the window and counter.
        void reset() noexcept;

        // Reconfigure the maximum rate and reset the window.
        void set_max_rate(int32_t max_per_second) noexcept;

        [[nodiscard]] int32_t max_rate() const noexcept { return max_per_second_; }

    private:
        int32_t max_per_second_;
        std::atomic<int32_t> count_{0};
        std::atomic<int64_t> window_start_{0}; // epoch seconds
    };

} // namespace sor::risk
