// Unit tests for FixedPoint<Scale>, PriceFixed, and FeeRate.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <limits>

#include "core/fixed_point.h"
#include "core/types.h"

using namespace sor;
using Catch::Approx;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("FixedPoint default construction is zero", "[fixed_point]")
{
    PriceFixed p;
    REQUIRE(p.raw() == 0);
    REQUIRE(p.to_double() == 0.0);
}

TEST_CASE("FixedPoint from_raw preserves raw value", "[fixed_point]")
{
    auto fp = PriceFixed::from_raw(12345678);
    REQUIRE(fp.raw() == 12345678);
}

TEST_CASE("FixedPoint from_double round-trips accurately", "[fixed_point]")
{
    SECTION("Positive value")
    {
        auto fp = PriceFixed::from_double(150.25);
        REQUIRE(fp.to_double() == Approx(150.25).epsilon(1e-7));
    }

    SECTION("Negative value")
    {
        auto fp = PriceFixed::from_double(-42.5);
        REQUIRE(fp.to_double() == Approx(-42.5).epsilon(1e-7));
    }

    SECTION("Zero")
    {
        auto fp = PriceFixed::from_double(0.0);
        REQUIRE(fp.raw() == 0);
        REQUIRE(fp.to_double() == 0.0);
    }

    SECTION("Small fractional value")
    {
        auto fp = PriceFixed::from_double(0.00000001);
        REQUIRE(fp.raw() == 1);
        REQUIRE(fp.to_double() == Approx(0.00000001).epsilon(1e-7));
    }
}

TEST_CASE("FixedPoint from_raw matches expected double", "[fixed_point]")
{
    // 100.50 * 1e8 = 10'050'000'000
    auto fp = PriceFixed::from_raw(10'050'000'000LL);
    REQUIRE(fp.to_double() == Approx(100.50).epsilon(1e-7));
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

TEST_CASE("FixedPoint addition", "[fixed_point]")
{
    auto a = PriceFixed::from_double(100.25);
    auto b = PriceFixed::from_double(50.75);
    auto c = a + b;
    REQUIRE(c.to_double() == Approx(151.0).epsilon(1e-7));
}

TEST_CASE("FixedPoint subtraction", "[fixed_point]")
{
    auto a = PriceFixed::from_double(100.0);
    auto b = PriceFixed::from_double(30.5);
    auto c = a - b;
    REQUIRE(c.to_double() == Approx(69.5).epsilon(1e-7));
}

TEST_CASE("FixedPoint subtraction yields negative", "[fixed_point]")
{
    auto a = PriceFixed::from_double(10.0);
    auto b = PriceFixed::from_double(20.0);
    auto c = a - b;
    REQUIRE(c.to_double() == Approx(-10.0).epsilon(1e-7));
}

TEST_CASE("FixedPoint multiplication", "[fixed_point]")
{
    auto a = PriceFixed::from_double(12.5);
    auto b = PriceFixed::from_double(4.0);
    auto c = a * b;
    REQUIRE(c.to_double() == Approx(50.0).epsilon(1e-6));
}

TEST_CASE("FixedPoint division", "[fixed_point]")
{
    auto a = PriceFixed::from_double(100.0);
    auto b = PriceFixed::from_double(4.0);
    auto c = a / b;
    REQUIRE(c.to_double() == Approx(25.0).epsilon(1e-6));
}

TEST_CASE("FixedPoint compound assignment operators", "[fixed_point]")
{
    SECTION("+=")
    {
        auto a = PriceFixed::from_double(10.0);
        a += PriceFixed::from_double(5.0);
        REQUIRE(a.to_double() == Approx(15.0).epsilon(1e-7));
    }

    SECTION("-=")
    {
        auto a = PriceFixed::from_double(10.0);
        a -= PriceFixed::from_double(3.0);
        REQUIRE(a.to_double() == Approx(7.0).epsilon(1e-7));
    }

    SECTION("*=")
    {
        auto a = PriceFixed::from_double(5.0);
        a *= PriceFixed::from_double(3.0);
        REQUIRE(a.to_double() == Approx(15.0).epsilon(1e-6));
    }

    SECTION("/=")
    {
        auto a = PriceFixed::from_double(20.0);
        a /= PriceFixed::from_double(4.0);
        REQUIRE(a.to_double() == Approx(5.0).epsilon(1e-6));
    }
}

TEST_CASE("FixedPoint unary negation", "[fixed_point]")
{
    auto a = PriceFixed::from_double(42.0);
    auto b = -a;
    REQUIRE(b.to_double() == Approx(-42.0).epsilon(1e-7));
    REQUIRE((-b).to_double() == Approx(42.0).epsilon(1e-7));
}

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

TEST_CASE("FixedPoint comparison operators", "[fixed_point]")
{
    auto a = PriceFixed::from_double(100.0);
    auto b = PriceFixed::from_double(200.0);
    auto c = PriceFixed::from_double(100.0);

    SECTION("equality")
    {
        REQUIRE(a == c);
        REQUIRE_FALSE(a == b);
    }

    SECTION("inequality via spaceship")
    {
        REQUIRE(a != b);
        REQUIRE_FALSE(a != c);
    }

    SECTION("less than")
    {
        REQUIRE(a < b);
        REQUIRE_FALSE(b < a);
        REQUIRE_FALSE(a < c);
    }

    SECTION("greater than")
    {
        REQUIRE(b > a);
        REQUIRE_FALSE(a > b);
    }

    SECTION("less than or equal")
    {
        REQUIRE(a <= c);
        REQUIRE(a <= b);
        REQUIRE_FALSE(b <= a);
    }

    SECTION("greater than or equal")
    {
        REQUIRE(b >= a);
        REQUIRE(a >= c);
        REQUIRE_FALSE(a >= b);
    }
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("FixedPoint edge cases", "[fixed_point]")
{
    SECTION("Very large value")
    {
        // ~92 billion, fits in int64 with 1e8 scale
        auto fp = PriceFixed::from_double(92'000'000.0);
        REQUIRE(fp.to_double() == Approx(92'000'000.0).epsilon(1e-7));
    }

    SECTION("Very small positive value")
    {
        auto fp = PriceFixed::from_double(0.00000001);
        REQUIRE(fp.raw() == 1);
    }

    SECTION("Negative zero")
    {
        auto fp = PriceFixed::from_double(-0.0);
        REQUIRE(fp.raw() == 0);
    }

    SECTION("Addition of zero")
    {
        auto a = PriceFixed::from_double(42.0);
        auto zero = PriceFixed::from_double(0.0);
        REQUIRE((a + zero) == a);
        REQUIRE((zero + a) == a);
    }

    SECTION("Multiplication by zero")
    {
        auto a = PriceFixed::from_double(42.0);
        auto zero = PriceFixed::from_double(0.0);
        REQUIRE((a * zero).raw() == 0);
    }

    SECTION("Multiplication by one")
    {
        auto a = PriceFixed::from_double(42.0);
        auto one = PriceFixed::from_double(1.0);
        REQUIRE((a * one).to_double() == Approx(42.0).epsilon(1e-6));
    }
}

// ---------------------------------------------------------------------------
// to_double precision
// ---------------------------------------------------------------------------

TEST_CASE("FixedPoint to_double precision for typical prices", "[fixed_point]")
{
    // Typical stock price
    auto price = PriceFixed::from_double(152.37);
    REQUIRE(price.to_double() == Approx(152.37).epsilon(1e-7));

    // Sub-penny pricing
    auto sub = PriceFixed::from_double(100.1234);
    REQUIRE(sub.to_double() == Approx(100.1234).epsilon(1e-7));
}

// ---------------------------------------------------------------------------
// PriceFixed and FeeRate aliases
// ---------------------------------------------------------------------------

TEST_CASE("PriceFixed uses 1e8 scale", "[fixed_point]")
{
    REQUIRE(PriceFixed::scale() == 100'000'000LL);
    auto p = PriceFixed::from_double(1.0);
    REQUIRE(p.raw() == 100'000'000LL);
}

TEST_CASE("FeeRate uses 1e6 scale", "[fixed_point]")
{
    REQUIRE(FeeRate::scale() == 1'000'000LL);

    // 10 basis points = 0.001
    auto fee = FeeRate::from_double(0.001);
    REQUIRE(fee.raw() == 1000);
    REQUIRE(fee.to_double() == Approx(0.001).epsilon(1e-6));
}

TEST_CASE("FeeRate arithmetic", "[fixed_point]")
{
    auto fee1 = FeeRate::from_double(0.001); // 10 bps
    auto fee2 = FeeRate::from_double(0.002); // 20 bps
    auto total = fee1 + fee2;
    REQUIRE(total.to_double() == Approx(0.003).epsilon(1e-6));
}

// ---------------------------------------------------------------------------
// Consistency with core/types.h helpers
// ---------------------------------------------------------------------------

TEST_CASE("to_price and to_double from types.h match FixedPoint", "[fixed_point]")
{
    double val = 123.456;
    Price p = to_price(val);
    auto fp = PriceFixed::from_double(val);

    // The raw representation should be the same since both use PRICE_SCALE=1e8
    REQUIRE(p == fp.raw());
    REQUIRE(to_double(p) == Approx(fp.to_double()).epsilon(1e-7));
}
