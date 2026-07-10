/// @file snr_calculator.cpp
/// @brief SNRCalculator implementation (radiometer equation, Sprint 10a).
///
/// Governing equations (docs/sprints/sprint_10a.md, Physics Reference):
///   S   = flux * area * efficiency * bandwidth        (source count rate)
///   SNR = S*t / sqrt((S + B)*t + read_noise_term) * sqrt(N_stations)
///   f_nu = 3631 * 10^(-0.4 * m_AB)  Jy                 (AB magnitude -> flux)

#include "instruments/snr_calculator.hpp"

#include "core/types.hpp"

#include <cmath>

namespace parallax::instruments
{
    namespace
    {
        /// AB-system flux zero point (Jansky) — flux of a magnitude-0 source.
        constexpr f64 kAbZeroPointJy = 3631.0;

        /// Read-noise term (electrons^2 * n_reads). Small, constant floor so that
        /// zero-exposure SNR is well-defined and short exposures are read-limited.
        constexpr f64 kReadNoiseTerm = 100.0;

        /// Sentinel returned when a target SNR is physically unreachable.
        constexpr f64 kUnreachableTimeS = 1.0e30;
    }

    f64 SNRCalculator::signal_rate(const SNRParameters& params)
    {
        // S = flux * area * efficiency * bandwidth.
        return params.target_flux_jy * params.collecting_area_m2 * params.efficiency *
               params.bandwidth_hz;
    }

    f64 SNRCalculator::compute_snr(const SNRParameters& params)
    {
        const f64 s = signal_rate(params);
        const f64 b = params.sky_background;
        const f64 t = params.integration_time_s;

        if (t <= 0.0 || s <= 0.0)
        {
            return 0.0;
        }

        const f64 noise_variance = (s + b) * t + kReadNoiseTerm;
        if (noise_variance <= 0.0)
        {
            return 0.0;
        }

        const u32 stations = params.num_stations > 0 ? params.num_stations : 1;
        const f64 station_gain = std::sqrt(static_cast<f64>(stations));

        // SNR = (S*t / sqrt(noise_variance)) * sqrt(N_stations).
        return (s * t / std::sqrt(noise_variance)) * station_gain;
    }

    f64 SNRCalculator::compute_snr_cumulative(const SNRParameters& params, f64 integration_time_s)
    {
        SNRParameters cumulative = params;
        cumulative.integration_time_s = integration_time_s;
        return compute_snr(cumulative);
    }

    f64 SNRCalculator::magnitude_to_flux_jy(f64 magnitude, f64 /*wavelength_nm*/)
    {
        // AB system: f_nu = 3631 * 10^(-0.4 * m)  Jy.
        // Single zero point across bands in this simplified model.
        return kAbZeroPointJy * std::pow(10.0, -0.4 * magnitude);
    }

    f64 SNRCalculator::time_to_reach_snr(f64 target_snr, const SNRParameters& params)
    {
        const f64 s = signal_rate(params);
        const f64 b = params.sky_background;

        if (target_snr <= 0.0)
        {
            return 0.0;
        }
        if (s <= 0.0)
        {
            return kUnreachableTimeS;
        }

        const u32 stations = params.num_stations > 0 ? params.num_stations : 1;
        const f64 n = static_cast<f64>(stations);

        // Solve  SNR = (S*t / sqrt((S+B)*t + R)) * sqrt(N)  for t.
        // Let k = SNR^2 / N. Then  S^2 t^2 = k * ((S+B) t + R), i.e.
        //   S^2 t^2 - k(S+B) t - kR = 0  -> quadratic a t^2 + b_c t + c = 0.
        const f64 k = (target_snr * target_snr) / n;
        const f64 a = s * s;
        const f64 b_coeff = -k * (s + b);
        const f64 c = -k * kReadNoiseTerm;

        const f64 discriminant = b_coeff * b_coeff - 4.0 * a * c;
        if (discriminant < 0.0)
        {
            return kUnreachableTimeS;
        }

        const f64 t = (-b_coeff + std::sqrt(discriminant)) / (2.0 * a);
        return t > 0.0 ? t : kUnreachableTimeS;
    }
}
