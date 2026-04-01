#include "engine_model.h"
#include <algorithm>

namespace luft
{

    EngineState EngineModel::update(double throttle_command,
                                    double current_thrust,
                                    double airspeed,
                                    double air_density,
                                    const AircraftParams &params,
                                    double dt) const
    {
        double throttle = clamp(throttle_command, 0.0, 1.0);
        double density_ratio = air_density / kSeaLevelDensity;

        double target_thrust = params.max_thrust * (params.idle_thrust_frac + (1.0 - params.idle_thrust_frac) * throttle) * density_ratio;

        double tau = params.engine_spool_tau;
        if (tau < 0.01)
            tau = 0.01;

        double thrust_dot = (target_thrust - current_thrust) / tau;
        double new_thrust = current_thrust + thrust_dot * dt;
        if (new_thrust < 0.0)
            new_thrust = 0.0;

        double fuel_flow = new_thrust * params.sfc;

        return {new_thrust, fuel_flow};
    }

} // namespace luft
