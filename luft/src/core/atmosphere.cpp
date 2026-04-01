#include "atmosphere.h"
#include "math_types.h"
#include <cmath>

namespace luft
{

    AtmosphereState Atmosphere::compute(double altitude_msl) const
    {
        double alt = altitude_msl;
        if (alt < 0.0)
            alt = 0.0;

        double T, P;

        if (alt <= kTropopauseAlt)
        {
            // Troposphere: linear temperature lapse
            T = kSeaLevelTemperature - kLapseRate * alt;
            double exp = kGravity / (kLapseRate * kGasConstantAir);
            P = kSeaLevelPressure * std::pow(T / kSeaLevelTemperature, exp);
        }
        else
        {
            // Tropopause temperature
            double T_trop = kSeaLevelTemperature - kLapseRate * kTropopauseAlt;
            double exp_trop = kGravity / (kLapseRate * kGasConstantAir);
            double P_trop = kSeaLevelPressure * std::pow(T_trop / kSeaLevelTemperature, exp_trop);

            // Lower stratosphere (11km - 20km): isothermal
            T = T_trop;
            double h_above = (alt > 20000.0 ? 20000.0 : alt) - kTropopauseAlt;
            P = P_trop * std::exp(-kGravity * h_above / (kGasConstantAir * T));
        }

        double rho = P / (kGasConstantAir * T);
        double a = std::sqrt(kGammaAir * kGasConstantAir * T);

        return {T, P, rho, a};
    }

} // namespace luft
