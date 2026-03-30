#pragma once

// High-resolution clock helpers for the Smart Order Router.

#include "core/types.h"

#include <string>
#include <chrono>

namespace sor::utils
{

    // ---------------------------------------------------------------------------
    // Clock helpers
    // ---------------------------------------------------------------------------

    /// Monotonic "now" for latency measurement.
    Timestamp now();

    /// Wall-clock "now" for external reporting / logging.
    WallTimestamp wall_now();

    // ---------------------------------------------------------------------------
    // Duration helpers
    // ---------------------------------------------------------------------------

    /// Microseconds elapsed since @p start (monotonic clock).
    std::chrono::microseconds elapsed_us(Timestamp start);

    /// Nanoseconds elapsed since @p start (monotonic clock).
    std::chrono::nanoseconds elapsed_ns(Timestamp start);

    // ---------------------------------------------------------------------------
    // String formatting
    // ---------------------------------------------------------------------------

    /// Format a wall-clock timestamp as "YYYY-MM-DD HH:MM:SS.uuuuuu".
    std::string format_timestamp(WallTimestamp ts);

    /// Format a duration in a human-friendly way, e.g. "123.456 us", "1.234 ms".
    std::string format_duration_us(std::chrono::microseconds us);

    // ---------------------------------------------------------------------------
    // Epoch conversion
    // ---------------------------------------------------------------------------

    /// Convert a wall-clock timestamp to microseconds since Unix epoch.
    int64_t to_epoch_us(WallTimestamp ts);

    /// Reconstruct a wall-clock timestamp from microseconds since Unix epoch.
    WallTimestamp from_epoch_us(int64_t us);

} // namespace sor::utils
