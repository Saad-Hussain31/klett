#include "utils/time_utils.h"

#include <ctime>
#include <cstdio>
#include <array>

namespace sor::utils
{

    // ---------------------------------------------------------------------------
    // Clock helpers
    // ---------------------------------------------------------------------------

    Timestamp now()
    {
        return std::chrono::steady_clock::now();
    }

    WallTimestamp wall_now()
    {
        return std::chrono::system_clock::now();
    }

    // ---------------------------------------------------------------------------
    // Duration helpers
    // ---------------------------------------------------------------------------

    std::chrono::microseconds elapsed_us(Timestamp start)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(now() - start);
    }

    std::chrono::nanoseconds elapsed_ns(Timestamp start)
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(now() - start);
    }

    // ---------------------------------------------------------------------------
    // String formatting
    // ---------------------------------------------------------------------------

    std::string format_timestamp(WallTimestamp ts)
    {
        using namespace std::chrono;

        const auto epoch = ts.time_since_epoch();
        const auto secs = duration_cast<seconds>(epoch);
        const auto us_part = duration_cast<microseconds>(epoch) - duration_cast<microseconds>(secs);

        const std::time_t tt = system_clock::to_time_t(ts);
        std::tm tm_buf{};
        ::gmtime_r(&tt, &tm_buf);

        // "YYYY-MM-DD HH:MM:SS.uuuuuu"
        std::array<char, 64> buf{};
        const int n = std::snprintf(
            buf.data(), buf.size(),
            "%04d-%02d-%02d %02d:%02d:%02d.%06ld",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            static_cast<long>(us_part.count()));

        return std::string(buf.data(), static_cast<std::size_t>(n));
    }

    std::string format_duration_us(std::chrono::microseconds us)
    {
        const auto count = us.count();

        std::array<char, 64> buf{};
        int n = 0;

        if (count < 1'000)
        {
            n = std::snprintf(buf.data(), buf.size(), "%ld us", static_cast<long>(count));
        }
        else if (count < 1'000'000)
        {
            n = std::snprintf(buf.data(), buf.size(), "%.3f ms",
                              static_cast<double>(count) / 1'000.0);
        }
        else
        {
            n = std::snprintf(buf.data(), buf.size(), "%.3f s",
                              static_cast<double>(count) / 1'000'000.0);
        }

        return std::string(buf.data(), static_cast<std::size_t>(n));
    }

    // ---------------------------------------------------------------------------
    // Epoch conversion
    // ---------------------------------------------------------------------------

    int64_t to_epoch_us(WallTimestamp ts)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   ts.time_since_epoch())
            .count();
    }

    WallTimestamp from_epoch_us(int64_t us)
    {
        return WallTimestamp{std::chrono::microseconds{us}};
    }

} // namespace sor::utils
