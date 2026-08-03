#include "interferometry/kolmogorov.hpp"

#include "core/types.hpp"

#include <cmath>

namespace parallax::interferometry
{
    std::vector<std::vector<double>> kolmogorov_series(
        std::size_t station_count,
        std::size_t k_samples,
        double rms_rad,
        Mulberry32& rng)
    {
        if (station_count == 0 || k_samples == 0)
        {
            return {};
        }

        std::vector<std::vector<double>> result(station_count, std::vector<double>(k_samples, 0.0));

        const double K = static_cast<double>(k_samples);

        for (std::size_t s = 0; s < station_count; ++s)
        {
            // ── Draw phases (binding draw order: station-major, mode-major) ──────
            // phase_m ∈ [0, 2π) for m = 1 .. kKolmogorovModes
            // Draws are always consumed regardless of rms_rad so that the error-RNG
            // stream position is identical for every call with the same station_count
            // and k_samples.
            double phases[kKolmogorovModes];
            for (int m = 0; m < kKolmogorovModes; ++m)
            {
                phases[m] = astro_constants::kTwoPi * rng.next();
            }

            if (rms_rad == 0.0)
            {
                // Series stays zero; draws were consumed above for RNG consistency.
                continue;
            }

            // ── Build raw series ──────────────────────────────────────────────────
            // amplitude_m = m^(-4/3) with m 1-indexed
            for (std::size_t k = 0; k < k_samples; ++k)
            {
                double ph = 0.0;
                for (int mi = 0; mi < kKolmogorovModes; ++mi)
                {
                    const int m = mi + 1; // 1-indexed mode number
                    const double amplitude = std::pow(static_cast<double>(m), -4.0 / 3.0);
                    // K > 1: argument varies over time; K == 1: k = 0 → argument = 0
                    const double arg = astro_constants::kTwoPi * static_cast<double>(m)
                        * static_cast<double>(k) / K
                        + phases[mi];
                    ph += amplitude * std::sin(arg);
                }
                result[s][k] = ph;
            }

            // ── Normalise to requested RMS ────────────────────────────────────────
            double sum_sq = 0.0;
            for (std::size_t k = 0; k < k_samples; ++k)
            {
                sum_sq += result[s][k] * result[s][k];
            }
            const double raw_rms = std::sqrt(sum_sq / static_cast<double>(k_samples));

            if (raw_rms > 0.0)
            {
                const double scale = rms_rad / raw_rms;
                for (std::size_t k = 0; k < k_samples; ++k)
                {
                    result[s][k] *= scale;
                }
            }
            // If raw_rms == 0 (pathological: all modes cancelled exactly), leave zeros.
        }

        return result;
    }
}
