#pragma once

// Core type definitions for the Smart Order Router.
// All types are designed for low-latency, zero-allocation hot paths.

#include <cstdint>
#include <chrono>
#include <limits>
#include <cstring>
#include <string_view>
#include <string>
#include <array>
#include <atomic>
#include <functional>

namespace sor
{

    // ---------------------------------------------------------------------------
    // Fundamental numeric types (fixed-point prices, quantities, identifiers)
    // ---------------------------------------------------------------------------

    /// Price in fixed-point representation with 8 decimal places.
    /// Multiply the real price by PRICE_SCALE to obtain the integer encoding.
    using Price = int64_t;

    /// Signed quantity (supports short-selling semantics).
    using Quantity = int64_t;

    /// Monotonically-increasing order identifier.
    using OrderId = uint64_t;

    /// Compact venue identifier (supports up to 65535 venues).
    using VenueId = uint16_t;

    /// Monotonic clock used for internal latency measurement.
    using Timestamp = std::chrono::steady_clock::time_point;

    /// Wall-clock timestamp for external reporting / logging.
    using WallTimestamp = std::chrono::system_clock::time_point;

    // ---------------------------------------------------------------------------
    // Constants
    // ---------------------------------------------------------------------------

    inline constexpr int64_t PRICE_SCALE = 100'000'000LL; // 1e8
    inline constexpr Price INVALID_PRICE = std::numeric_limits<Price>::max();
    inline constexpr OrderId INVALID_ORDER_ID = 0;

    // ---------------------------------------------------------------------------
    // Conversion helpers
    // ---------------------------------------------------------------------------

    /// Convert a floating-point price to the fixed-point representation.
    [[nodiscard]] inline Price to_price(double d) noexcept
    {
        // Use 0.5 bias for correct rounding towards nearest.
        return static_cast<Price>(d * static_cast<double>(PRICE_SCALE) +
                                  (d >= 0.0 ? 0.5 : -0.5));
    }

    /// Convert a fixed-point price back to double.
    [[nodiscard]] inline double to_double(Price p) noexcept
    {
        return static_cast<double>(p) / static_cast<double>(PRICE_SCALE);
    }

    // ---------------------------------------------------------------------------
    // Enumerations
    // ---------------------------------------------------------------------------

    enum class Side : uint8_t
    {
        Buy = 0,
        Sell = 1,
    };

    enum class OrderType : uint8_t
    {
        Limit = 0,
        Market = 1,
        IOC = 2, // Immediate-or-Cancel
        FOK = 3, // Fill-or-Kill
    };

    enum class TimeInForce : uint8_t
    {
        GTC = 0, // Good-Til-Canceled
        IOC = 1, // Immediate-or-Cancel
        FOK = 2, // Fill-or-Kill
        GTD = 3, // Good-Til-Date
        DAY = 4, // Day order
    };

    enum class OrderState : uint8_t
    {
        New = 0,
        PendingNew = 1,
        Accepted = 2,
        PartiallyFilled = 3,
        Filled = 4,
        PendingCancel = 5,
        Canceled = 6,
        Rejected = 7,
        Expired = 8,
        PendingReplace = 9,
    };

    enum class RoutingStrategy : uint8_t
    {
        BestPrice = 0,
        LiquiditySweep = 1,
        SmartIOC = 2,
        VWAP = 3,
    };

    enum class VenueStatus : uint8_t
    {
        Connected = 0,
        Disconnected = 1,
        Degraded = 2,
    };

    // ---------------------------------------------------------------------------
    // FixedString<N> -- stack-allocated, null-terminated string. No heap allocs.
    // ---------------------------------------------------------------------------

    template <std::size_t N>
    struct FixedString
    {
        static_assert(N > 0 && N <= 255, "FixedString capacity must be in [1, 255]");

        // -- Data ---------------------------------------------------------------
        char data_[N + 1]{}; // +1 for null terminator
        uint8_t len_{0};

        // -- Constructors -------------------------------------------------------
        constexpr FixedString() noexcept = default;

        /*implicit*/ FixedString(const char *s) noexcept
        { // NOLINT
            assign(s);
        }

        /*implicit*/ FixedString(std::string_view sv) noexcept
        { // NOLINT
            assign(sv);
        }

        // -- Assignment ---------------------------------------------------------
        FixedString &operator=(const char *s) noexcept
        {
            assign(s);
            return *this;
        }

        FixedString &operator=(std::string_view sv) noexcept
        {
            assign(sv);
            return *this;
        }

        void assign(const char *s) noexcept
        {
            if (!s)
            {
                clear();
                return;
            }
            assign(std::string_view{s});
        }

        void assign(std::string_view sv) noexcept
        {
            const auto copy_len = sv.size() < N ? sv.size() : N;
            std::memcpy(data_, sv.data(), copy_len);
            data_[copy_len] = '\0';
            len_ = static_cast<uint8_t>(copy_len);
        }

        void clear() noexcept
        {
            data_[0] = '\0';
            len_ = 0;
        }

        // -- Access -------------------------------------------------------------
        [[nodiscard]] const char *c_str() const noexcept { return data_; }
        [[nodiscard]] std::string_view to_string_view() const noexcept { return {data_, len_}; }
        [[nodiscard]] std::string to_string() const { return std::string(data_, len_); }
        [[nodiscard]] uint8_t size() const noexcept { return len_; }
        [[nodiscard]] bool empty() const noexcept { return len_ == 0; }
        [[nodiscard]] static constexpr std::size_t max_size() noexcept { return N; }

        // -- Comparison ---------------------------------------------------------
        [[nodiscard]] bool operator==(const FixedString &rhs) const noexcept
        {
            return len_ == rhs.len_ && std::memcmp(data_, rhs.data_, len_) == 0;
        }
        [[nodiscard]] bool operator!=(const FixedString &rhs) const noexcept
        {
            return !(*this == rhs);
        }
        [[nodiscard]] bool operator<(const FixedString &rhs) const noexcept
        {
            const auto cmp = std::memcmp(data_, rhs.data_, std::min(len_, rhs.len_));
            return cmp < 0 || (cmp == 0 && len_ < rhs.len_);
        }
        [[nodiscard]] bool operator==(std::string_view sv) const noexcept
        {
            return to_string_view() == sv;
        }
        [[nodiscard]] bool operator!=(std::string_view sv) const noexcept
        {
            return to_string_view() != sv;
        }
    };

    // Convenient type aliases.
    using Symbol = FixedString<16>;
    using VenueName = FixedString<32>;
    using ClientId = FixedString<32>;

} // namespace sor

// ---------------------------------------------------------------------------
// std::hash specializations so FixedString can be used in unordered containers
// ---------------------------------------------------------------------------
namespace std
{

    template <std::size_t N>
    struct hash<sor::FixedString<N>>
    {
        std::size_t operator()(const sor::FixedString<N> &fs) const noexcept
        {
            return std::hash<std::string_view>{}(fs.to_string_view());
        }
    };

} // namespace std
