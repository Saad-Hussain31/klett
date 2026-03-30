#pragma once

/// @file Common.h
/// @brief Common definitions for the concurrency framework.
///
/// Performance notes:
/// - CACHE_LINE_SIZE is used to pad atomics and prevent false sharing.
///   Modern x86-64 CPUs use 64-byte cache lines. When two threads write
///   to different variables that happen to share a cache line, the hardware
///   bounces the line between cores (false sharing), destroying performance.
/// - alignas(CACHE_LINE_SIZE) on hot atomic variables ensures each lives
///   on its own cache line.

#include <cstddef>
#include <cstdint>
#include <functional>

namespace conc
{

    /// Hardware cache line size. 64 bytes on x86-64 and most ARM64.
    /// Used to pad structures and prevent false sharing between threads.
    inline constexpr std::size_t CACHE_LINE_SIZE = 64;

    /// Rounds up to the nearest power of two. Used for ring buffer sizing
    /// so we can use bitmask instead of modulo (bitwise AND is ~1 cycle,
    /// integer division is ~20-40 cycles).
    constexpr std::size_t next_power_of_two(std::size_t v)
    {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;
        return v;
    }

    /// Check if a value is a power of two.
    constexpr bool is_power_of_two(std::size_t v)
    {
        return v && !(v & (v - 1));
    }

} // namespace conc
