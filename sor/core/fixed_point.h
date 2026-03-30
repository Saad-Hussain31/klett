#pragma once

// Fixed-point arithmetic class for fee-adjusted pricing and precise calculations.
// Avoids floating-point rounding issues critical in financial computations.

#include <cstdint>
#include <type_traits>
#include <compare>

namespace sor
{

    // ---------------------------------------------------------------------------
    // FixedPoint<Scale> -- compile-time-scaled fixed-point number.
    //
    // The raw integer value represents (real_value * Scale).
    // For prices:  Scale = 100'000'000  (8 decimal places)
    // For fees:    Scale = 1'000'000    (6 decimal places)
    // ---------------------------------------------------------------------------

    template <int64_t Scale>
    class FixedPoint
    {
        static_assert(Scale > 0, "Scale must be positive");

    public:
        // -- Construction -------------------------------------------------------
        constexpr FixedPoint() noexcept = default;

        /// Build from a raw integer that is already scaled.
        [[nodiscard]] static constexpr FixedPoint from_raw(int64_t raw) noexcept
        {
            FixedPoint fp;
            fp.raw_ = raw;
            return fp;
        }

        /// Build from a double value.
        [[nodiscard]] static FixedPoint from_double(double d) noexcept
        {
            FixedPoint fp;
            fp.raw_ = static_cast<int64_t>(
                d * static_cast<double>(Scale) + (d >= 0.0 ? 0.5 : -0.5));
            return fp;
        }

        // -- Access -------------------------------------------------------------
        [[nodiscard]] constexpr int64_t raw() const noexcept { return raw_; }

        [[nodiscard]] double to_double() const noexcept
        {
            return static_cast<double>(raw_) / static_cast<double>(Scale);
        }

        // -- Arithmetic (same-scale) -------------------------------------------
        constexpr FixedPoint operator+(FixedPoint rhs) const noexcept
        {
            return from_raw(raw_ + rhs.raw_);
        }

        constexpr FixedPoint operator-(FixedPoint rhs) const noexcept
        {
            return from_raw(raw_ - rhs.raw_);
        }

        /// Multiplication: (a * Scale) * (b * Scale) / Scale = a*b * Scale
        /// We use __int128 to avoid overflow in the intermediate product.
        constexpr FixedPoint operator*(FixedPoint rhs) const noexcept
        {
            __int128 prod = static_cast<__int128>(raw_) * rhs.raw_;
            return from_raw(static_cast<int64_t>(prod / Scale));
        }

        /// Division: (a * Scale) / (b * Scale) * Scale = a/b * Scale
        constexpr FixedPoint operator/(FixedPoint rhs) const noexcept
        {
            __int128 num = static_cast<__int128>(raw_) * Scale;
            return from_raw(static_cast<int64_t>(num / rhs.raw_));
        }

        // -- Compound assignment -----------------------------------------------
        constexpr FixedPoint &operator+=(FixedPoint rhs) noexcept
        {
            raw_ += rhs.raw_;
            return *this;
        }

        constexpr FixedPoint &operator-=(FixedPoint rhs) noexcept
        {
            raw_ -= rhs.raw_;
            return *this;
        }

        constexpr FixedPoint &operator*=(FixedPoint rhs) noexcept
        {
            *this = *this * rhs;
            return *this;
        }

        constexpr FixedPoint &operator/=(FixedPoint rhs) noexcept
        {
            *this = *this / rhs;
            return *this;
        }

        // -- Unary negation ----------------------------------------------------
        constexpr FixedPoint operator-() const noexcept
        {
            return from_raw(-raw_);
        }

        // -- Comparison (C++20 spaceship) --------------------------------------
        constexpr auto operator<=>(const FixedPoint &rhs) const noexcept = default;
        constexpr bool operator==(const FixedPoint &rhs) const noexcept = default;

        // -- Scale accessor (useful in generic code) ---------------------------
        static constexpr int64_t scale() noexcept { return Scale; }

    private:
        int64_t raw_{0};
    };

    // ---------------------------------------------------------------------------
    // Common type aliases
    // ---------------------------------------------------------------------------

    /// 8-decimal-place fixed-point for prices.
    using PriceFixed = FixedPoint<100'000'000>;

    /// 6-decimal-place fixed-point for fee rates (e.g. 0.000100 = 1 bps).
    using FeeRate = FixedPoint<1'000'000>;

} // namespace sor
