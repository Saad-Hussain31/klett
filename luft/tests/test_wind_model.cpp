#include <gtest/gtest.h>
#include "wind_model.h"
#include "math_types.h"
#include <cmath>

using namespace luft;

TEST(WindModel, DefaultReturnsZero)
{
    WindModel model;
    auto ws = model.compute(1000.0, 0.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.x, 0.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.y, 0.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.z, 0.0);
}

TEST(WindModel, ConstantWindReturnsSetValue)
{
    WindModel model;
    Vec3 wind(5.0, -3.0, 1.0);
    model.set_constant(wind);

    auto ws = model.compute(1000.0, 0.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.x, 5.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.y, -3.0);
    EXPECT_DOUBLE_EQ(ws.velocity_ned.z, 1.0);
}

TEST(WindModel, ConstantWindDoesNotVaryWithTime)
{
    WindModel model;
    Vec3 wind(10.0, 0.0, 0.0);
    model.set_constant(wind);

    auto ws1 = model.compute(1000.0, 0.0);
    auto ws2 = model.compute(1000.0, 100.0);
    EXPECT_DOUBLE_EQ(ws1.velocity_ned.x, ws2.velocity_ned.x);
    EXPECT_DOUBLE_EQ(ws1.velocity_ned.y, ws2.velocity_ned.y);
    EXPECT_DOUBLE_EQ(ws1.velocity_ned.z, ws2.velocity_ned.z);
}

TEST(WindModel, ConstantWindDoesNotVaryWithAltitude)
{
    WindModel model;
    Vec3 wind(5.0, 3.0, 0.0);
    model.set_constant(wind);

    auto ws1 = model.compute(0.0, 0.0);
    auto ws2 = model.compute(10000.0, 0.0);
    EXPECT_DOUBLE_EQ(ws1.velocity_ned.x, ws2.velocity_ned.x);
    EXPECT_DOUBLE_EQ(ws1.velocity_ned.y, ws2.velocity_ned.y);
}

TEST(WindModel, GustsMagnitudeVariesOverTime)
{
    WindModel model;
    Vec3 base(5.0, 0.0, 0.0);
    model.set_with_gusts(base, 0.5);

    auto ws1 = model.compute(1000.0, 0.0);
    auto ws2 = model.compute(1000.0, 10.0);
    auto ws3 = model.compute(1000.0, 20.0);

    // With gusts, the wind should differ at different times
    // At least one pair should differ
    bool differs = (ws1.velocity_ned.x != ws2.velocity_ned.x) ||
                   (ws2.velocity_ned.x != ws3.velocity_ned.x);
    EXPECT_TRUE(differs);
}

TEST(WindModel, GustIntensityZeroMatchesConstant)
{
    WindModel model_const;
    Vec3 base(5.0, 3.0, -1.0);
    model_const.set_constant(base);

    WindModel model_gust;
    model_gust.set_with_gusts(base, 0.0);

    auto ws_const = model_const.compute(1000.0, 5.0);
    auto ws_gust = model_gust.compute(1000.0, 5.0);
    EXPECT_DOUBLE_EQ(ws_const.velocity_ned.x, ws_gust.velocity_ned.x);
    EXPECT_DOUBLE_EQ(ws_const.velocity_ned.y, ws_gust.velocity_ned.y);
    EXPECT_DOUBLE_EQ(ws_const.velocity_ned.z, ws_gust.velocity_ned.z);
}

TEST(WindModel, GustsStrongerNearGround)
{
    WindModel model;
    model.set_with_gusts(Vec3::zero(), 0.5);

    // Compute gust magnitudes at different altitudes for a specific time
    double t = 5.0;
    auto ws_low = model.compute(100.0, t);
    auto ws_high = model.compute(5000.0, t);

    double mag_low = ws_low.velocity_ned.magnitude();
    double mag_high = ws_high.velocity_ned.magnitude();

    // At altitude < 300m, there's an altitude scaling factor > 1.0
    // At altitude > 300m, factor is 1.0
    // So low altitude gust should have >= high altitude gust magnitude
    // (assuming the base wind is zero)
    EXPECT_GE(mag_low, mag_high - 1e-6);
}

TEST(WindModel, SetConstantDisablesGusts)
{
    WindModel model;
    model.set_with_gusts({5.0, 0.0, 0.0}, 0.8);

    // Verify gusts are active (wind differs at different times)
    EXPECT_NE(model.compute(100.0, 1.0).velocity_ned.x,
              model.compute(100.0, 10.0).velocity_ned.x);

    // Now set constant (should disable gusts)
    model.set_constant({5.0, 0.0, 0.0});
    auto ws3 = model.compute(100.0, 1.0);
    auto ws4 = model.compute(100.0, 10.0);
    EXPECT_DOUBLE_EQ(ws3.velocity_ned.x, ws4.velocity_ned.x);
    EXPECT_DOUBLE_EQ(ws3.velocity_ned.y, ws4.velocity_ned.y);
    EXPECT_DOUBLE_EQ(ws3.velocity_ned.z, ws4.velocity_ned.z);
}

TEST(WindModel, GustComponentsHaveReasonableMagnitude)
{
    WindModel model;
    model.set_with_gusts(Vec3::zero(), 1.0); // max intensity

    // Over a range of times, gust components should remain bounded
    for (double t = 0.0; t < 100.0; t += 1.0)
    {
        auto ws = model.compute(500.0, t);
        // Each sinusoidal component has amplitude <= gust_intensity * alt_factor * 1.0
        // With intensity=1.0 and alt_factor=1.0 (above 300m), max is ~1.0 per axis
        EXPECT_LT(std::abs(ws.velocity_ned.x), 2.0);
        EXPECT_LT(std::abs(ws.velocity_ned.y), 2.0);
        EXPECT_LT(std::abs(ws.velocity_ned.z), 2.0);
    }
}
