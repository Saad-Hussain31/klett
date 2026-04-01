#pragma once

#include "aircraft_state.h"

namespace luft
{

    struct EngineState
    {
        double thrust;
        double fuel_flow_rate;
    };

    class EngineModel
    {
    public:
        EngineState update(double throttle_command,
                           double current_thrust,
                           double airspeed,
                           double air_density,
                           const AircraftParams &params,
                           double dt) const;
    };

} // namespace luft
