#pragma once

#include "aircraft_state.h"
#include "atmosphere.h"
#include "wind_model.h"
#include "aerodynamics.h"
#include "engine_model.h"

namespace luft
{

    class FlightDynamics
    {
    public:
        void update(AircraftState &state,
                    const AircraftParams &params,
                    const ControlInput &input,
                    const AtmosphereState &atmo,
                    const WindState &wind,
                    double dt);

    private:
        Aerodynamics aero_;
        EngineModel engine_;

        struct StateDerivatives
        {
            Vec3 position_dot;
            Vec3 velocity_body_dot;
            Quaternion orientation_dot;
            Vec3 omega_dot;
        };

        StateDerivatives compute_derivatives(const AircraftState &state,
                                             const AircraftParams &params,
                                             const ControlInput &input,
                                             const AtmosphereState &atmo,
                                             const WindState &wind) const;
    };

} // namespace luft
