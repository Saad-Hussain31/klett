#include "risk/rate_limiter.h"

namespace sor::risk
{

    RateLimiter::RateLimiter(int32_t max_per_second) noexcept
        : max_per_second_(max_per_second)
    {
        // Initialise the window to the current second so the first call
        // does not unconditionally reset.
        const auto now = std::chrono::steady_clock::now();
        window_start_.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch())
                .count(),
            std::memory_order_relaxed);
    }

    bool RateLimiter::try_acquire() noexcept
    {
        const auto now = std::chrono::steady_clock::now();
        const int64_t now_sec =
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch())
                .count();

        // If the second has rolled over, reset the counter.
        // CAS avoids the thundering-herd problem when many threads race
        // to flip the window at the same instant.
        int64_t expected = window_start_.load(std::memory_order_relaxed);
        if (now_sec > expected)
        {
            if (window_start_.compare_exchange_strong(
                    expected, now_sec, std::memory_order_acq_rel))
            {
                // We won the race to advance the window -- reset counter.
                count_.store(1, std::memory_order_relaxed);
                return true;
            }
            // Another thread advanced the window; fall through and try to
            // increment the counter normally.
        }

        // Increment within the current window.
        const int32_t prev = count_.fetch_add(1, std::memory_order_relaxed);
        if (prev >= max_per_second_)
        {
            // Over limit -- undo the increment to keep the counter accurate.
            count_.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    void RateLimiter::reset() noexcept
    {
        count_.store(0, std::memory_order_relaxed);
        const auto now = std::chrono::steady_clock::now();
        window_start_.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch())
                .count(),
            std::memory_order_relaxed);
    }

    void RateLimiter::set_max_rate(int32_t max_per_second) noexcept
    {
        max_per_second_ = max_per_second;
        reset();
    }

} // namespace sor::risk
