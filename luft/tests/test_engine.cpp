#include <gtest/gtest.h>
#include "engine_model.h"
#include "aircraft_state.h"
#include "math_types.h"

using namespace luft;

class EngineModelTest : public ::testing::Test
{
protected:
    EngineModel engine;
    AircraftParams params;
    double dt = 0.01;
};

TEST_F(EngineModelTest, ZeroThrottleIdleThrust)
{
    double current_thrust = 0.0;
    auto es = engine.update(0.0, current_thrust, 50.0, kSeaLevelDensity, params, dt);
    // After one step, thrust should move toward idle target
    EXPECT_GT(es.thrust, 0.0);
    // Should be heading toward idle target, not full
    EXPECT_LT(es.thrust, params.max_thrust * 0.1);
}

TEST_F(EngineModelTest, FullThrottleApproachesMaxThrust)
{
    // Run engine at full throttle for many steps
    double thrust = 0.0;
    for (int i = 0; i < 1000; ++i)
    {
        auto es = engine.update(1.0, thrust, 50.0, kSeaLevelDensity, params, dt);
        thrust = es.thrust;
    }
    // After 10 seconds, thrust should be very close to max at sea level
    double target = params.max_thrust * 1.0; // density_ratio = 1.0, full throttle
    EXPECT_NEAR(thrust, target, target * 0.01);
}

TEST_F(EngineModelTest, FirstOrderLagNotInstant)
{
    // Starting from zero, one step should not reach target
    double target = params.max_thrust;
    auto es = engine.update(1.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    EXPECT_GT(es.thrust, 0.0);
    EXPECT_LT(es.thrust, target * 0.5); // should be much less than target after one step
}

TEST_F(EngineModelTest, FuelConsumptionPositive)
{
    auto es = engine.update(0.5, 1000.0, 50.0, kSeaLevelDensity, params, dt);
    EXPECT_GT(es.fuel_flow_rate, 0.0);
}

TEST_F(EngineModelTest, FuelConsumptionProportionalToThrust)
{
    auto es_low = engine.update(0.0, 100.0, 50.0, kSeaLevelDensity, params, dt);
    auto es_high = engine.update(1.0, 2000.0, 50.0, kSeaLevelDensity, params, dt);
    // Higher thrust should produce higher fuel flow
    EXPECT_GT(es_high.fuel_flow_rate, es_low.fuel_flow_rate);
}

TEST_F(EngineModelTest, AltitudeEffectReducesThrust)
{
    // At higher altitude, density is lower -> less thrust
    double sea_level_density = kSeaLevelDensity;
    double high_alt_density = 0.4; // roughly at 10km

    double thrust = 0.0;
    // Run at sea level
    for (int i = 0; i < 1000; ++i)
    {
        auto es = engine.update(1.0, thrust, 50.0, sea_level_density, params, dt);
        thrust = es.thrust;
    }
    double sea_thrust = thrust;

    thrust = 0.0;
    // Run at altitude
    for (int i = 0; i < 1000; ++i)
    {
        auto es = engine.update(1.0, thrust, 50.0, high_alt_density, params, dt);
        thrust = es.thrust;
    }
    double alt_thrust = thrust;

    EXPECT_LT(alt_thrust, sea_thrust);
}

TEST_F(EngineModelTest, ThrustNeverNegative)
{
    // Even with aggressive deceleration, thrust should not go negative
    auto es = engine.update(0.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    EXPECT_GE(es.thrust, 0.0);
}

TEST_F(EngineModelTest, ThrottleClamped)
{
    // Throttle values beyond [0,1] should be clamped
    auto es_neg = engine.update(-1.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    auto es_zero = engine.update(0.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    EXPECT_NEAR(es_neg.thrust, es_zero.thrust, 1e-6);

    auto es_over = engine.update(2.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    auto es_one = engine.update(1.0, 0.0, 50.0, kSeaLevelDensity, params, dt);
    EXPECT_NEAR(es_over.thrust, es_one.thrust, 1e-6);
}

TEST_F(EngineModelTest, SpoolTimeConstant)
{
    // After one time constant (tau = 1.0s = 100 steps at dt=0.01),
    // thrust should be approximately 63.2% of target (1 - e^-1)
    double thrust = 0.0;
    int steps = static_cast<int>(params.engine_spool_tau / dt);
    for (int i = 0; i < steps; ++i)
    {
        auto es = engine.update(1.0, thrust, 50.0, kSeaLevelDensity, params, dt);
        thrust = es.thrust;
    }
    double target = params.max_thrust;
    double expected_fraction = 1.0 - std::exp(-1.0);
    EXPECT_NEAR(thrust / target, expected_fraction, 0.05);
}
