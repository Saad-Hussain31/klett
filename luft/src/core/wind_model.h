#pragma once

#include "math_types.h"

namespace luft
{

    struct WindState
    {
        Vec3 velocity_ned;
    };

    class WindModel
    {
    public:
        void set_constant(Vec3 wind_ned);
        void set_with_gusts(Vec3 base_wind_ned, double gust_intensity);

        WindState compute(double altitude_msl, double sim_time) const;

    private:
        Vec3 base_wind_{};
        double gust_intensity_ = 0.0;
        bool gusts_enabled_ = false;
    };

} // namespace luft
