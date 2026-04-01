#include <gtest/gtest.h>
#include "flight_dynamics.h"
#include "atmosphere.h"
#include "wind_model.h"
#include "aircraft_state.h"
#include "math_types.h"
#include <cmath>

using namespace luft;

class FlightDynamicsTest : public ::testing::Test
{
protected:
    FlightDynamics dynamics;
    AircraftParams params;
    Atmosphere atmosphere;
    WindModel wind_model;

    AtmosphereState sea_level_atmo()
    {
        return atmosphere.compute(0.0);
    }

    WindState no_wind()
    {
        return wind_model.compute(0.0, 0.0);
    }
};

TEST_F(FlightDynamicsTest, GravityFreeFall)
{
    // Aircraft with zero airspeed at altitude, should fall under gravity.
    // Use a short time window (< 0.1s) so velocity stays below the 1 m/s
    // aero threshold, ensuring pure gravity with no drag.
    AircraftState state;
    state.position = {0.0, 0.0, -1000.0};  // 1000m altitude
    state.velocity_body = {0.0, 0.0, 0.0}; // stationary
    state.orientation = Quaternion::identity();
    state.angular_velocity = Vec3::zero();
    state.airspeed = 0.0;
    state.alpha = 0.0;
    state.beta = 0.0;
    state.dynamic_pressure = 0.0;
    state.thrust_current = 0.0;
    state.fuel_mass = 100.0;
    state.altitude_msl = 1000.0;

    ControlInput input;
    double dt = 0.001;
    double total_time = 0.05; // short enough that |v| < 1.0 (aero threshold)
    int steps = static_cast<int>(total_time / dt);

    auto atmo = atmosphere.compute(1000.0);
    auto wind = no_wind();

    for (int i = 0; i < steps; ++i)
    {
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    // Pure gravity: v_z = g * t = 9.80665 * 0.05 ~ 0.49 m/s (below aero threshold)
    double expected_vz = kGravity * total_time;
    EXPECT_NEAR(state.velocity_body.z, expected_vz, 1e-4);
}

TEST_F(FlightDynamicsTest, RK4AccuracyFreeFall)
{
    // Test RK4 accuracy for pure gravity (no aero, no thrust).
    // Keep time short (< 0.1s) so body speed stays below 1 m/s aero threshold.
    // Analytical: z = z0 + 0.5*g*t^2, v = g*t
    AircraftState state;
    state.position = {0.0, 0.0, -5000.0}; // 5000m altitude
    state.velocity_body = {0.0, 0.0, 0.0};
    state.orientation = Quaternion::identity();
    state.angular_velocity = Vec3::zero();
    state.airspeed = 0.0;
    state.alpha = 0.0;
    state.beta = 0.0;
    state.dynamic_pressure = 0.0;
    state.thrust_current = 0.0;
    state.fuel_mass = 100.0;
    state.altitude_msl = 5000.0;

    ControlInput input;
    double dt = 0.001;
    double total_time = 0.05; // short enough for pure gravity
    int steps = static_cast<int>(total_time / dt);

    auto atmo = atmosphere.compute(5000.0);
    auto wind = no_wind();

    double z0 = state.position.z;
    for (int i = 0; i < steps; ++i)
    {
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    // Analytical position change in NED z: delta_z = 0.5 * g * t^2
    double expected_delta_z = 0.5 * kGravity * total_time * total_time;
    double actual_delta_z = state.position.z - z0;
    // RK4 should integrate constant acceleration exactly
    EXPECT_NEAR(actual_delta_z, expected_delta_z, 1e-6);
}

TEST_F(FlightDynamicsTest, QuaternionRemainsUnitAfterManySteps)
{
    AircraftState state;
    state.position = {0.0, 0.0, -2000.0};
    state.velocity_body = {50.0, 0.0, 0.0};
    state.orientation = Quaternion::from_euler(0.1, 0.05, 0.2);
    state.angular_velocity = {0.1, 0.05, 0.02};
    state.airspeed = 50.0;
    state.altitude_msl = 2000.0;
    state.dynamic_pressure = 0.5 * kSeaLevelDensity * 50.0 * 50.0;
    state.thrust_current = 500.0;
    state.fuel_mass = 100.0;

    ControlInput input;
    input.elevator = 0.1;
    input.throttle = 0.5;

    double dt = 0.01;
    auto atmo = atmosphere.compute(2000.0);
    auto wind = no_wind();

    for (int i = 0; i < 10000; ++i)
    {
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    double q_norm = state.orientation.norm();
    EXPECT_NEAR(q_norm, 1.0, 1e-6);
}

TEST_F(FlightDynamicsTest, GroundContact)
{
    // Aircraft very close to ground, moving downward
    AircraftState state;
    state.position = {0.0, 0.0, -5.0};      // 5m altitude
    state.velocity_body = {10.0, 0.0, 5.0}; // moving forward and down in body frame
    state.orientation = Quaternion::identity();
    state.angular_velocity = Vec3::zero();
    state.airspeed = 10.0;
    state.altitude_msl = 5.0;
    state.dynamic_pressure = 0.5 * kSeaLevelDensity * 10.0 * 10.0;
    state.thrust_current = 0.0;
    state.fuel_mass = 100.0;

    ControlInput input;
    double dt = 0.01;
    auto atmo = atmosphere.compute(5.0);
    auto wind = no_wind();

    // Run enough steps to hit the ground
    for (int i = 0; i < 500; ++i)
    {
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    // Altitude should be clamped to >= 0
    EXPECT_GE(state.altitude_msl, 0.0);
    EXPECT_LE(state.position.z, 0.0); // NED: z <= 0 means altitude >= 0
}

TEST_F(FlightDynamicsTest, StableFlightDoesNotDiverge)
{
    // Start from reasonable conditions, check it doesn't blow up in 10 seconds
    AircraftState state;
    state.position = {0.0, 0.0, -1000.0};
    state.velocity_body = {50.0, 0.0, 0.0};
    state.orientation = Quaternion::identity();
    state.angular_velocity = Vec3::zero();
    state.airspeed = 50.0;
    state.altitude_msl = 1000.0;
    state.dynamic_pressure = 0.5 * kSeaLevelDensity * 50.0 * 50.0;
    state.thrust_current = 1000.0;
    state.fuel_mass = 100.0;

    ControlInput input;
    input.throttle = 0.5;

    double dt = 0.01;
    int steps = 1000; // 10 seconds

    for (int i = 0; i < steps; ++i)
    {
        auto atmo = atmosphere.compute(state.altitude_msl);
        auto wind = no_wind();
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    // Should not have NaN
    EXPECT_FALSE(std::isnan(state.position.x));
    EXPECT_FALSE(std::isnan(state.position.y));
    EXPECT_FALSE(std::isnan(state.position.z));
    EXPECT_FALSE(std::isnan(state.velocity_body.x));
    EXPECT_FALSE(std::isnan(state.velocity_body.y));
    EXPECT_FALSE(std::isnan(state.velocity_body.z));
    // Speed should remain reasonable (< Mach 1)
    EXPECT_LT(state.velocity_body.magnitude(), 500.0);
}

TEST_F(FlightDynamicsTest, FuelDepletion)
{
    AircraftState state;
    state.position = {0.0, 0.0, -1000.0};
    state.velocity_body = {50.0, 0.0, 0.0};
    state.orientation = Quaternion::identity();
    state.angular_velocity = Vec3::zero();
    state.airspeed = 50.0;
    state.altitude_msl = 1000.0;
    state.dynamic_pressure = 0.5 * kSeaLevelDensity * 50.0 * 50.0;
    state.thrust_current = 2000.0;
    state.fuel_mass = 0.001; // almost empty

    ControlInput input;
    input.throttle = 1.0;

    double dt = 0.01;
    auto atmo = atmosphere.compute(1000.0);
    auto wind = no_wind();

    // Run a few steps to exhaust fuel
    for (int i = 0; i < 100; ++i)
    {
        dynamics.update(state, params, input, atmo, wind, dt);
    }

    EXPECT_EQ(state.fuel_mass, 0.0);
    EXPECT_EQ(state.thrust_current, 0.0);
}
