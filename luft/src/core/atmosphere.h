#pragma once

namespace luft
{

    struct AtmosphereState
    {
        double temperature;
        double pressure;
        double density;
        double speed_of_sound;
    };

    class Atmosphere
    {
    public:
        AtmosphereState compute(double altitude_msl) const;
    };

} // namespace luft
