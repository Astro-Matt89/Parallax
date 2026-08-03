#pragma once

#include "interferometry/mulberry32.hpp"

#include <cstddef>
#include <vector>

namespace parallax::interferometry
{
    // Number of sinusoidal modes in the Kolmogorov approximation (normative).
    inline constexpr int kKolmogorovModes = 12;

    /// Generate per-station Kolmogorov atmospheric phase time series.
    ///
    /// Each station receives a K-sample phase series whose power spectrum
    /// approximates a Kolmogorov turbulence spectrum:
    ///
    ///   amplitude_m = m^(-4/3)   for m = 1 .. M  (M = kKolmogorovModes)
    ///   phase_m     = 2π * rng.next()             (drawn station-major, mode-major)
    ///   ph[k]       = Σ_m amplitude_m * sin(2π m k/K + phase_m)
    ///
    /// The raw series is then normalised to the requested RMS (rms_rad).
    /// If rms_rad == 0 the output is all-zeros, but RNG draws for the phase
    /// offsets are still consumed so that the error-generator stream stays
    /// consistent across calls with different rms values.
    ///
    /// Generation order (binding contract for fixture compatibility):
    ///   for s = 0 .. station_count-1:
    ///     for m = 1 .. kKolmogorovModes:
    ///       phase_m = 2π * rng.next()    ← one draw per mode per station
    ///
    /// Returns a [station_count × k_samples] matrix (row = station).
    /// Returns an empty vector when station_count == 0 or k_samples == 0.
    [[nodiscard]] std::vector<std::vector<double>> kolmogorov_series(
        std::size_t station_count,
        std::size_t k_samples,
        double rms_rad,
        Mulberry32& rng);
}
