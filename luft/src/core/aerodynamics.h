#pragma once

#include "aircraft_state.h"

namespace luft
{

    class Aerodynamics
    {
    public:
        ForcesAndMoments compute(const AircraftState &state,
                                 const AircraftParams &params,
                                 const ControlInput &input,
                                 double air_density) const;
    };

} // namespace luft
