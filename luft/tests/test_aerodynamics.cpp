#include <gtest/gtest.h>
#include "aerodynamics.h"
#include "aircraft_state.h"
#include "math_types.h"
#include <cmath>

using namespace luft;

class AerodynamicsTest : public ::testing::Test
{
protected:
    Aerodynamics aero;
    AircraftParams params;
    ControlInput input;

    // Build a level-flight state at a given airspeed
    AircraftState make_state(double airspeed, double alpha = 0.0, double beta = 0.0)
    {
        AircraftState s;
        s.airspeed = airspeed;
        s.alpha = alpha;
        s.beta = beta;
        s.dynamic_pressure = 0.5 * kSeaLevelDensity * airspeed * airspeed;
        s.velocity_body = {airspeed, 0.0, 0.0};
        s.angular_velocity = Vec3::zero();
        return s;
    }
};

TEST_F(AerodynamicsTest, ZeroAirspeedProducesZeroForces)
{
    AircraftState state = make_state(0.0);
    auto fm = aero.compute(state, params, input, kSeaLevelDensity);
    EXPECT_DOUBLE_EQ(fm.force.x, 0.0);
    EXPECT_DOUBLE_EQ(fm.force.y, 0.0);
    EXPECT_DOUBLE_EQ(fm.force.z, 0.0);
    EXPECT_DOUBLE_EQ(fm.moment.x, 0.0);
    EXPECT_DOUBLE_EQ(fm.moment.y, 0.0);
    EXPECT_DOUBLE_EQ(fm.moment.z, 0.0);
}

TEST_F(AerodynamicsTest, VeryLowAirspeedProducesZeroForces)
{
    AircraftState state = make_state(0.5); // below 1.0 threshold
    auto fm = aero.compute(state, params, input, kSeaLevelDensity);
    EXPECT_DOUBLE_EQ(fm.force.x, 0.0);
    EXPECT_DOUBLE_EQ(fm.force.y, 0.0);
    EXPECT_DOUBLE_EQ(fm.force.z, 0.0);
}

TEST_F(AerodynamicsTest, PositiveAlphaIncreasesLift)
{
    AircraftState state_low = make_state(50.0, 0.0);
    AircraftState state_high = make_state(50.0, 0.1);
    auto fm_low = aero.compute(state_low, params, input, kSeaLevelDensity);
    auto fm_high = aero.compute(state_high, params, input, kSeaLevelDensity);
    // Lift is in negative z direction (body frame: down is positive z)
    // More alpha -> more negative force.z (more lift)
    EXPECT_LT(fm_high.force.z, fm_low.force.z);
}

TEST_F(AerodynamicsTest, DragIsPositiveOpposingMotion)
{
    // At zero alpha, drag opposes forward motion: force.x should be negative
    AircraftState state = make_state(50.0, 0.0);
    auto fm = aero.compute(state, params, input, kSeaLevelDensity);
    // force.x = -D*cos(alpha) + L*sin(alpha). At alpha=0, force.x = -D
    // D = qbar*S*CD, CD = CD0 + CDi_k*CL^2 > 0, so force.x < 0
    EXPECT_LT(fm.force.x, 0.0);
}

TEST_F(AerodynamicsTest, ElevatorProducesPitchMoment)
{
    AircraftState state = make_state(50.0, 0.0);
    // Positive elevator input (nose up)
    ControlInput input_up;
    input_up.elevator = 1.0;
    auto fm_up = aero.compute(state, params, input_up, kSeaLevelDensity);

    // Cmde is -1.122 (negative), delta_e = elevator * max_elevator > 0
    // So Cm contribution from elevator = Cmde * delta_e < 0
    // This means pitching moment is nose-down for positive elevator in this sign convention
    // Moment.y corresponds to pitching moment
    ControlInput input_none;
    auto fm_none = aero.compute(state, params, input_none, kSeaLevelDensity);
    // The elevator deflection should change the pitching moment
    EXPECT_NE(fm_up.moment.y, fm_none.moment.y);

    // With Cmde < 0 and positive elevator, the pitching moment should decrease
    EXPECT_LT(fm_up.moment.y, fm_none.moment.y);
}

TEST_F(AerodynamicsTest, AileronProducesRollMoment)
{
    AircraftState state = make_state(50.0, 0.0);
    ControlInput input_right;
    input_right.aileron = 1.0;
    auto fm = aero.compute(state, params, input_right, kSeaLevelDensity);

    ControlInput input_none;
    auto fm_none = aero.compute(state, params, input_none, kSeaLevelDensity);
    // Clda = -0.178 (negative), positive aileron * max_aileron > 0
    // So Cl contribution < 0, meaning roll moment is non-zero
    EXPECT_NE(fm.moment.x, fm_none.moment.x);
}

TEST_F(AerodynamicsTest, RudderProducesYawMoment)
{
    AircraftState state = make_state(50.0, 0.0);
    ControlInput input_rudder;
    input_rudder.rudder = 1.0;
    auto fm = aero.compute(state, params, input_rudder, kSeaLevelDensity);

    ControlInput input_none;
    auto fm_none = aero.compute(state, params, input_none, kSeaLevelDensity);
    // Cndr = -0.074 (negative), so positive rudder produces negative yaw moment
    EXPECT_NE(fm.moment.z, fm_none.moment.z);
}

TEST_F(AerodynamicsTest, SymmetricFlightNoLateralForces)
{
    // Zero sideslip, no aileron/rudder -> zero side force and zero roll/yaw moments
    AircraftState state = make_state(50.0, 0.05, 0.0); // small alpha, zero beta
    ControlInput input_sym;
    auto fm = aero.compute(state, params, input_sym, kSeaLevelDensity);
    EXPECT_NEAR(fm.force.y, 0.0, 1e-6);
    EXPECT_NEAR(fm.moment.x, 0.0, 1e-6);
    EXPECT_NEAR(fm.moment.z, 0.0, 1e-6);
}

TEST_F(AerodynamicsTest, SideslipProducesSideForce)
{
    AircraftState state = make_state(50.0, 0.0, 0.1); // beta = 0.1 rad
    auto fm = aero.compute(state, params, input, kSeaLevelDensity);
    // Cyb = -0.31, beta > 0 -> CY < 0 -> side force negative
    EXPECT_LT(fm.force.y, 0.0);
}

TEST_F(AerodynamicsTest, LiftApproximatesWeightAtTrim)
{
    // Find the approximate trim alpha for level flight at 50 m/s
    // Weight = (757 + 100) * 9.80665 ~ 8404 N
    // L = qbar * S * CL, qbar = 0.5 * 1.225 * 50^2 = 1531.25
    // Need CL = W / (qbar * S) = 8404 / (1531.25 * 16.2) ~ 0.339
    // CL = CL0 + CLa * alpha -> alpha = (CL - CL0) / CLa = (0.339 - 0.307) / 4.73 ~ 0.0068 rad
    double weight = (params.empty_mass + 100.0) * kGravity;
    double qbar = 0.5 * kSeaLevelDensity * 50.0 * 50.0;
    double CL_needed = weight / (qbar * params.wing_area);
    double alpha_trim = (CL_needed - params.CL0) / params.CLa;

    AircraftState state = make_state(50.0, alpha_trim);
    auto fm = aero.compute(state, params, input, kSeaLevelDensity);
    // Lift is -force.z at small alpha (approximately)
    double lift = -fm.force.z;
    EXPECT_NEAR(lift, weight, weight * 0.05); // within 5%
}

TEST_F(AerodynamicsTest, FlapsIncreaseLift)
{
    AircraftState state = make_state(50.0, 0.05);
    ControlInput no_flaps;
    ControlInput with_flaps;
    with_flaps.flaps = 1.0;
    auto fm_no = aero.compute(state, params, no_flaps, kSeaLevelDensity);
    auto fm_flaps = aero.compute(state, params, with_flaps, kSeaLevelDensity);
    // CLdf > 0 and flaps > 0 -> more lift -> more negative force.z
    EXPECT_LT(fm_flaps.force.z, fm_no.force.z);
}
