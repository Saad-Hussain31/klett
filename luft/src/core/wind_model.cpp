#include "wind_model.h"
#include <cmath>

namespace luft
{

    void WindModel::set_constant(Vec3 wind_ned)
    {
        base_wind_ = wind_ned;
        gust_intensity_ = 0.0;
        gusts_enabled_ = false;
    }

    void WindModel::set_with_gusts(Vec3 base_wind_ned, double gust_intensity)
    {
        base_wind_ = base_wind_ned;
        gust_intensity_ = gust_intensity;
        gusts_enabled_ = true;
    }

    WindState WindModel::compute(double altitude_msl, double sim_time) const
    {
        Vec3 wind = base_wind_;

        if (gusts_enabled_ && gust_intensity_ > 0.0)
        {
            // Dryden-inspired sinusoidal gust model with multiple frequencies
            // to approximate turbulence without expensive random generation.
            double t = sim_time;
            double gi = gust_intensity_;

            // Altitude-based scaling: turbulence increases near ground
            double alt_factor = 1.0;
            if (altitude_msl < 300.0)
            {
                alt_factor = 1.0 + 0.5 * (1.0 - altitude_msl / 300.0);
            }

            double gust_n = gi * alt_factor * (0.50 * std::sin(0.30 * t + 1.0) + 0.30 * std::sin(0.77 * t + 3.7) + 0.20 * std::sin(1.83 * t + 0.5));
            double gust_e = gi * alt_factor * (0.50 * std::sin(0.40 * t + 2.3) + 0.30 * std::sin(0.93 * t + 5.1) + 0.20 * std::sin(2.17 * t + 1.2));
            double gust_d = gi * alt_factor * 0.5 * (0.60 * std::sin(0.35 * t + 4.0) + 0.40 * std::sin(1.10 * t + 2.8));

            wind.x += gust_n;
            wind.y += gust_e;
            wind.z += gust_d;
        }

        return {wind};
    }

} // namespace luft
