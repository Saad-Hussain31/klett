#include "aerodynamics.h"
#include <cmath>

namespace luft
{

    ForcesAndMoments Aerodynamics::compute(const AircraftState &state,
                                           const AircraftParams &params,
                                           const ControlInput &input,
                                           double air_density) const
    {
        double V = state.airspeed;
        if (V < 1.0)
        {
            return {};
        }

        double alpha = state.alpha;
        double beta = state.beta;
        double qbar = state.dynamic_pressure;

        double S = params.wing_area;
        double b = params.wing_span;
        double c = params.mean_chord;

        // Control surface deflections (radians)
        double delta_e = input.elevator * params.max_elevator;
        double delta_a = input.aileron * params.max_aileron;
        double delta_r = input.rudder * params.max_rudder;
        double delta_f = input.flaps * params.max_flap;

        // Non-dimensional angular rates
        double p = state.angular_velocity.x;
        double q = state.angular_velocity.y;
        double r = state.angular_velocity.z;
        double inv2V = 1.0 / (2.0 * V);
        double p_hat = p * b * inv2V;
        double q_hat = q * c * inv2V;
        double r_hat = r * b * inv2V;

        // Aerodynamic coefficients
        double CL = params.CL0 + params.CLa * alpha + params.CLde * delta_e + params.CLq * q_hat + params.CLdf * delta_f;

        double CD = params.CD0 + params.CDi_k * CL * CL;

        double CY = params.Cyb * beta + params.Cydr * delta_r;

        double Cl = params.Clb * beta + params.Clp * p_hat + params.Clr * r_hat + params.Clda * delta_a + params.Cldr * delta_r;

        double Cm = params.Cm0 + params.Cma * alpha + params.Cmde * delta_e + params.Cmq * q_hat + params.Cmdf * delta_f;

        double Cn = params.Cnb * beta + params.Cnp * p_hat + params.Cnr * r_hat + params.Cnda * delta_a + params.Cndr * delta_r;

        // Forces: stability-axis to body-axis transformation
        double L = qbar * S * CL; // lift
        double D = qbar * S * CD; // drag

        double cos_a = std::cos(alpha);
        double sin_a = std::sin(alpha);

        ForcesAndMoments fm;
        fm.force.x = -D * cos_a + L * sin_a;
        fm.force.y = qbar * S * CY;
        fm.force.z = -D * sin_a - L * cos_a;

        fm.moment.x = qbar * S * b * Cl;
        fm.moment.y = qbar * S * c * Cm;
        fm.moment.z = qbar * S * b * Cn;

        return fm;
    }

} // namespace luft
