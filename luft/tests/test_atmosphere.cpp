#include <gtest/gtest.h>
#include "atmosphere.h"
#include "math_types.h"
#include <cmath>

using namespace luft;

class AtmosphereTest : public ::testing::Test
{
protected:
    Atmosphere atmo;
};

TEST_F(AtmosphereTest, SeaLevelTemperature)
{
    auto state = atmo.compute(0.0);
    EXPECT_NEAR(state.temperature, 288.15, 1e-6);
}

TEST_F(AtmosphereTest, SeaLevelPressure)
{
    auto state = atmo.compute(0.0);
    EXPECT_NEAR(state.pressure, 101325.0, 1e-1);
}

TEST_F(AtmosphereTest, SeaLevelDensity)
{
    auto state = atmo.compute(0.0);
    EXPECT_NEAR(state.density, 1.225, 1e-2);
}

TEST_F(AtmosphereTest, SeaLevelSpeedOfSound)
{
    auto state = atmo.compute(0.0);
    // a = sqrt(gamma * R * T) = sqrt(1.4 * 287.058 * 288.15) ~ 340.3 m/s
    EXPECT_NEAR(state.speed_of_sound, 340.3, 0.5);
}

TEST_F(AtmosphereTest, TropopauseTemperature)
{
    auto state = atmo.compute(11000.0);
    EXPECT_NEAR(state.temperature, 216.65, 1e-2);
}

TEST_F(AtmosphereTest, AboveTropopauseIsothermal)
{
    auto at_11k = atmo.compute(11000.0);
    auto at_15k = atmo.compute(15000.0);
    EXPECT_NEAR(at_15k.temperature, 216.65, 1e-2);
    EXPECT_NEAR(at_15k.temperature, at_11k.temperature, 1e-2);
}

TEST_F(AtmosphereTest, NegativeAltitudeClampedToSeaLevel)
{
    // Implementation clamps alt < 0 to 0
    auto below = atmo.compute(-500.0);
    auto sea = atmo.compute(0.0);
    EXPECT_NEAR(below.temperature, sea.temperature, 1e-6);
    EXPECT_NEAR(below.pressure, sea.pressure, 1e-6);
    EXPECT_NEAR(below.density, sea.density, 1e-6);
}

TEST_F(AtmosphereTest, DensityDecreasesWithAltitude)
{
    auto sea = atmo.compute(0.0);
    auto mid = atmo.compute(5000.0);
    auto high = atmo.compute(10000.0);
    EXPECT_GT(sea.density, mid.density);
    EXPECT_GT(mid.density, high.density);
}

TEST_F(AtmosphereTest, PressureDecreasesWithAltitude)
{
    auto sea = atmo.compute(0.0);
    auto high = atmo.compute(5000.0);
    EXPECT_GT(sea.pressure, high.pressure);
}

TEST_F(AtmosphereTest, TemperatureDecreasesInTroposphere)
{
    auto low = atmo.compute(0.0);
    auto high = atmo.compute(5000.0);
    EXPECT_GT(low.temperature, high.temperature);
}

TEST_F(AtmosphereTest, SpeedOfSoundDecreasesWithAltitude)
{
    auto low = atmo.compute(0.0);
    auto high = atmo.compute(10000.0);
    EXPECT_GT(low.speed_of_sound, high.speed_of_sound);
}

TEST_F(AtmosphereTest, MidAltitudeConsistency)
{
    // At 5000m, verify T = 288.15 - 0.0065 * 5000 = 255.65 K
    auto state = atmo.compute(5000.0);
    EXPECT_NEAR(state.temperature, 255.65, 1e-2);
}

TEST_F(AtmosphereTest, IdealGasLawConsistency)
{
    // rho = P / (R * T) should hold at any altitude
    for (double alt : {0.0, 3000.0, 8000.0, 11000.0, 15000.0})
    {
        auto state = atmo.compute(alt);
        double rho_calc = state.pressure / (kGasConstantAir * state.temperature);
        EXPECT_NEAR(state.density, rho_calc, 1e-6)
            << "Ideal gas law failed at altitude " << alt;
    }
}
