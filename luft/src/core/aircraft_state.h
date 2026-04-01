#pragma once

#include "math_types.h"
#include <cstdint>

namespace luft
{

    // ──────────────────────────────────────────────
    // Control input — normalized [-1, 1] or [0, 1]
    // ──────────────────────────────────────────────

    struct ControlInput
    {
        double elevator = 0.0; // [-1, 1] nose down to nose up
        double aileron = 0.0;  // [-1, 1] left roll to right roll
        double rudder = 0.0;   // [-1, 1] left yaw to right yaw
        double throttle = 0.0; // [0, 1]
        double flaps = 0.0;    // [0, 1]
        double trim = 0.0;     // [-1, 1] elevator trim

        void clamp_all()
        {
            elevator = clamp(elevator, -1.0, 1.0);
            aileron = clamp(aileron, -1.0, 1.0);
            rudder = clamp(rudder, -1.0, 1.0);
            throttle = clamp(throttle, 0.0, 1.0);
            flaps = clamp(flaps, 0.0, 1.0);
            trim = clamp(trim, -1.0, 1.0);
        }
    };

    // ──────────────────────────────────────────────
    // Aircraft parameters — defines the aircraft type
    // ──────────────────────────────────────────────

    struct AircraftParams
    {
        // Geometry
        double wing_area = 16.2;  // m^2 (Cessna 172 approx)
        double wing_span = 11.0;  // m
        double mean_chord = 1.49; // m (wing_area / wing_span)
        double tail_arm = 4.4;    // m — distance from CG to tail AC

        // Mass properties
        double empty_mass = 757.0;    // kg
        double max_fuel_mass = 163.0; // kg
        double Ixx = 1285.3;          // kg*m^2 (roll inertia)
        double Iyy = 1824.9;          // kg*m^2 (pitch inertia)
        double Izz = 2666.9;          // kg*m^2 (yaw inertia)
        double Ixz = 0.0;             // kg*m^2 (cross product of inertia)

        // Longitudinal stability derivatives
        double CL0 = 0.307; // lift at zero AoA
        double CLa = 4.73;  // lift curve slope (per rad)
        double CLde = 0.43; // lift due to elevator (per rad)
        double CLq = 3.9;   // lift due to pitch rate
        double CLdf = 0.8;  // lift due to flaps (per rad)

        double CD0 = 0.027;   // parasite drag
        double CDi_k = 0.054; // induced drag factor (1 / (pi * e * AR))

        double Cm0 = 0.04;    // pitching moment at zero AoA
        double Cma = -0.613;  // pitch stability (per rad) — negative = stable
        double Cmde = -1.122; // pitch control power (per rad)
        double Cmq = -12.4;   // pitch damping
        double Cmdf = -0.1;   // pitching moment due to flaps

        // Lateral-directional stability derivatives
        double Cyb = -0.31;  // side force due to sideslip (per rad)
        double Cydr = 0.187; // side force due to rudder (per rad)

        double Clb = -0.089;  // roll moment due to sideslip (per rad)
        double Clp = -0.47;   // roll damping
        double Clr = 0.096;   // roll due to yaw rate
        double Clda = -0.178; // roll control power (per rad)
        double Cldr = 0.0147; // roll due to rudder (per rad)

        double Cnb = 0.065;   // yaw stability (per rad) — positive = stable
        double Cnp = -0.03;   // yaw due to roll rate
        double Cnr = -0.099;  // yaw damping
        double Cnda = 0.02;   // adverse yaw (per rad)
        double Cndr = -0.074; // yaw control power (per rad)

        // Control surface max deflections (radians)
        double max_elevator = 28.0 * kDegToRad;
        double max_aileron = 20.0 * kDegToRad;
        double max_rudder = 16.0 * kDegToRad;
        double max_flap = 40.0 * kDegToRad;

        // Engine
        double max_thrust = 2400.0; // N (approx Lycoming O-360 equivalent)
        double idle_thrust_frac = 0.03;
        double engine_spool_tau = 1.0; // seconds — time constant for thrust response
        double sfc = 0.00008;          // specific fuel consumption (kg/(N*s))
    };

    // ──────────────────────────────────────────────
    // Aircraft state — full dynamic state
    // ──────────────────────────────────────────────

    struct AircraftState
    {
        // Position in NED frame (meters) relative to origin
        Vec3 position{0.0, 0.0, -1000.0}; // start at 1000m altitude (NED: z is down)

        // Velocity in body frame (m/s)
        Vec3 velocity_body{50.0, 0.0, 0.0}; // initial forward speed

        // Orientation: body-to-NED quaternion
        Quaternion orientation = Quaternion::identity();

        // Angular velocity in body frame (rad/s)
        Vec3 angular_velocity{0.0, 0.0, 0.0};

        // Engine state
        double thrust_current = 0.0; // current thrust (N), lags throttle
        double fuel_mass = 100.0;    // kg remaining

        // Derived / cached quantities (updated each frame)
        double airspeed = 50.0;        // m/s (magnitude of velocity_body minus wind in body)
        double alpha = 0.0;            // angle of attack (rad)
        double beta = 0.0;             // sideslip angle (rad)
        double altitude_msl = 1000.0;  // meters above sea level
        double dynamic_pressure = 0.0; // 0.5 * rho * V^2

        // Total mass
        double total_mass(const AircraftParams &params) const
        {
            return params.empty_mass + fuel_mass;
        }

        // Convenience: velocity in NED frame
        Vec3 velocity_ned() const
        {
            return orientation.rotate(velocity_body);
        }
    };

    // ──────────────────────────────────────────────
    // Simulation runtime state
    // ──────────────────────────────────────────────

    enum class SimState : uint8_t
    {
        Uninitialized = 0,
        Initialized,
        Running,
        Paused,
        Stopped,
        Error
    };

    inline const char *sim_state_name(SimState s)
    {
        switch (s)
        {
        case SimState::Uninitialized:
            return "Uninitialized";
        case SimState::Initialized:
            return "Initialized";
        case SimState::Running:
            return "Running";
        case SimState::Paused:
            return "Paused";
        case SimState::Stopped:
            return "Stopped";
        case SimState::Error:
            return "Error";
        }
        return "Unknown";
    }

    // ──────────────────────────────────────────────
    // Forces and moments accumulator
    // ──────────────────────────────────────────────

    struct ForcesAndMoments
    {
        Vec3 force{};  // body frame (N)
        Vec3 moment{}; // body frame (N*m)
    };

} // namespace luft
