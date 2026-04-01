#include "flight_dynamics.h"
#include <cmath>

namespace luft
{

    FlightDynamics::StateDerivatives FlightDynamics::compute_derivatives(
        const AircraftState &state,
        const AircraftParams &params,
        const ControlInput &input,
        const AtmosphereState &atmo,
        const WindState &wind) const
    {

        StateDerivatives d;

        // Position derivative: body velocity rotated to NED
        d.position_dot = state.orientation.rotate(state.velocity_body);

        // Compute airspeed-related quantities in body frame
        Vec3 wind_body = state.orientation.inverse_rotate(wind.velocity_ned);
        Vec3 air_vel = state.velocity_body - wind_body;
        double V = air_vel.magnitude();

        // Build a temporary state with updated aero quantities for the aero model
        AircraftState aero_state = state;
        aero_state.airspeed = V;
        if (V > 1.0)
        {
            aero_state.alpha = std::atan2(air_vel.z, air_vel.x);
            aero_state.beta = std::asin(clamp(air_vel.y / V, -1.0, 1.0));
            aero_state.dynamic_pressure = 0.5 * atmo.density * V * V;
        }
        else
        {
            aero_state.alpha = 0.0;
            aero_state.beta = 0.0;
            aero_state.dynamic_pressure = 0.0;
        }

        // Aerodynamic forces and moments
        ForcesAndMoments aero_fm = aero_.compute(aero_state, params, input, atmo.density);

        // Engine thrust along body x-axis
        double thrust = state.thrust_current;
        Vec3 thrust_force{thrust, 0.0, 0.0};

        // Gravity in NED, then rotate to body frame
        Vec3 gravity_ned{0.0, 0.0, state.total_mass(params) * kGravity};
        Vec3 gravity_body = state.orientation.inverse_rotate(gravity_ned);

        // Total forces in body frame
        double mass = state.total_mass(params);
        Vec3 total_force = aero_fm.force + thrust_force + gravity_body;

        // Body-frame velocity derivative: F/m - omega x V
        Vec3 omega = state.angular_velocity;
        d.velocity_body_dot = total_force / mass - omega.cross(state.velocity_body);

        // Orientation derivative: dq/dt = 0.5 * q * omega_quat
        d.orientation_dot = state.orientation.derivative(omega);

        // Angular acceleration: Euler's equation
        // For simplicity, treating Ixz coupling with the full inversion
        double Ixx = params.Ixx;
        double Iyy = params.Iyy;
        double Izz = params.Izz;
        double Ixz = params.Ixz;

        Vec3 M = aero_fm.moment;
        double p = omega.x, q = omega.y, r = omega.z;

        // omega x (I * omega)
        double Hx = Ixx * p - Ixz * r;
        double Hy = Iyy * q;
        double Hz = Izz * r - Ixz * p;
        Vec3 gyro_term = omega.cross({Hx, Hy, Hz});

        // Right-hand side: M - omega x H
        double rhs_x = M.x - gyro_term.x;
        double rhs_y = M.y - gyro_term.y;
        double rhs_z = M.z - gyro_term.z;

        // Invert the inertia matrix [Ixx, 0, -Ixz; 0, Iyy, 0; -Ixz, 0, Izz]
        double det = Ixx * Izz - Ixz * Ixz;
        if (std::abs(det) < 1e-12)
            det = 1e-12;
        double inv_det = 1.0 / det;

        d.omega_dot.x = inv_det * (Izz * rhs_x + Ixz * rhs_z);
        d.omega_dot.y = rhs_y / Iyy;
        d.omega_dot.z = inv_det * (Ixz * rhs_x + Ixx * rhs_z);

        return d;
    }

    void FlightDynamics::update(AircraftState &state,
                                const AircraftParams &params,
                                const ControlInput &input,
                                const AtmosphereState &atmo,
                                const WindState &wind,
                                double dt)
    {
        // --- Engine update (outside RK4, first-order lag is fine at outer level) ---
        EngineState eng = engine_.update(input.throttle, state.thrust_current,
                                         state.airspeed, atmo.density, params, dt);
        state.thrust_current = eng.thrust;

        // Fuel consumption
        double fuel_used = eng.fuel_flow_rate * dt;
        state.fuel_mass -= fuel_used;
        if (state.fuel_mass <= 0.0)
        {
            state.fuel_mass = 0.0;
            state.thrust_current = 0.0;
        }

        // --- RK4 integration ---
        // Save initial state
        Vec3 pos0 = state.position;
        Vec3 vel0 = state.velocity_body;
        Quaternion ori0 = state.orientation;
        Vec3 omg0 = state.angular_velocity;

        auto apply_step = [&](const StateDerivatives &d, double h)
        {
            state.position = pos0 + d.position_dot * h;
            state.velocity_body = vel0 + d.velocity_body_dot * h;
            state.orientation = (ori0 + d.orientation_dot * h).normalized();
            state.angular_velocity = omg0 + d.omega_dot * h;
        };

        // k1
        StateDerivatives k1 = compute_derivatives(state, params, input, atmo, wind);

        // k2
        apply_step(k1, dt * 0.5);
        StateDerivatives k2 = compute_derivatives(state, params, input, atmo, wind);

        // k3
        apply_step(k2, dt * 0.5);
        StateDerivatives k3 = compute_derivatives(state, params, input, atmo, wind);

        // k4
        apply_step(k3, dt);
        StateDerivatives k4 = compute_derivatives(state, params, input, atmo, wind);

        // Combine: state = state0 + (dt/6) * (k1 + 2*k2 + 2*k3 + k4)
        double dt6 = dt / 6.0;
        state.position = pos0 + (k1.position_dot + k2.position_dot * 2.0 + k3.position_dot * 2.0 + k4.position_dot) * dt6;

        state.velocity_body = vel0 + (k1.velocity_body_dot + k2.velocity_body_dot * 2.0 + k3.velocity_body_dot * 2.0 + k4.velocity_body_dot) * dt6;

        Quaternion ori_dot_avg = (k1.orientation_dot + k2.orientation_dot * 2.0 + k3.orientation_dot * 2.0 + k4.orientation_dot) * dt6;
        state.orientation = (ori0 + ori_dot_avg).normalized();

        state.angular_velocity = omg0 + (k1.omega_dot + k2.omega_dot * 2.0 + k3.omega_dot * 2.0 + k4.omega_dot) * dt6;

        // --- Update derived quantities ---
        state.altitude_msl = -state.position.z; // NED: z down, altitude up

        // Ground contact
        if (state.altitude_msl < 0.0)
        {
            state.position.z = 0.0;
            state.altitude_msl = 0.0;
            if (state.velocity_body.z > 0.0)
            {
                state.velocity_body.z = 0.0;
            }
            Vec3 vel_ned = state.orientation.rotate(state.velocity_body);
            if (vel_ned.z > 0.0)
            {
                vel_ned.z = 0.0;
                state.velocity_body = state.orientation.inverse_rotate(vel_ned);
            }
        }

        // Airspeed, alpha, beta, dynamic pressure
        Vec3 wind_body = state.orientation.inverse_rotate(wind.velocity_ned);
        Vec3 air_vel = state.velocity_body - wind_body;
        double V = air_vel.magnitude();
        state.airspeed = V;
        if (V > 1.0)
        {
            state.alpha = std::atan2(air_vel.z, air_vel.x);
            state.beta = std::asin(clamp(air_vel.y / V, -1.0, 1.0));
        }
        else
        {
            state.alpha = 0.0;
            state.beta = 0.0;
        }
        state.dynamic_pressure = 0.5 * atmo.density * V * V;
    }

} // namespace luft
