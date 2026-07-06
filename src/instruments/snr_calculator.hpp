#pragma once

/// @file snr_calculator.hpp
/// @brief Physical SNR model (radiometer equation) for the array instrument.
///
/// Replaces the flat mock SNR rate (5.0/hour) with real radiometry. See
/// docs/sprints/sprint_10a.md "Physics Reference" for the governing equations.

#include "core/types.hpp"

namespace parallax::instruments
{
    /// @brief Inputs to the radiometer-equation SNR model.
    struct SNRParameters
    {
        f64 target_flux_jy = 0.0;         ///< Target flux density (Jansky) or mag-derived.
        f64 collecting_area_m2 = 0.0;     ///< Total active aperture area (m^2).
        f64 efficiency = 1.0;             ///< System efficiency (0..1).
        f64 bandwidth_hz = 0.0;           ///< Spectral bandwidth (Hz).
        f64 integration_time_s = 0.0;     ///< Exposure time (seconds).
        f64 system_temperature_k = 0.0;   ///< Noise temperature (atmosphere + instrument).
        f64 sky_background = 0.0;         ///< Background count rate (same units as signal rate).
        u32 num_stations = 1;             ///< Active station count (for noise averaging).
    };

    /// @brief Radiometer-equation SNR calculator.
    ///
    /// The signal rate scales with flux, collecting area, efficiency and
    /// bandwidth; noise follows Poisson statistics on signal + background with a
    /// read-noise floor, reduced by sqrt(N_stations) to model station averaging.
    class SNRCalculator
    {
    public:
        /// @brief Compute SNR for the given parameters (radiometer equation).
        ///
        /// SNR = S*t / ( sqrt((S + B)*t + read_noise_term) / sqrt(N_stations) )
        /// where S is the source count rate and B the background count rate.
        [[nodiscard]] static f64 compute_snr(const SNRParameters& params);

        /// @brief Compute cumulative SNR after a given integration time.
        ///
        /// Convenience overload that overrides params.integration_time_s with the
        /// supplied elapsed time. Used by observation sessions ticking over time.
        [[nodiscard]] static f64 compute_snr_cumulative(
            const SNRParameters& params, f64 integration_time_s);

        /// @brief Convert an AB magnitude to flux density in Jansky.
        ///
        /// f_nu = 3631 * 10^(-0.4 * magnitude)  [Jy]. The wavelength argument is
        /// accepted for band-appropriate zero points; the AB system uses a single
        /// zero point (3631 Jy) across bands in this simplified model.
        [[nodiscard]] static f64 magnitude_to_flux_jy(f64 magnitude, f64 wavelength_nm);

        /// @brief Integration time needed to reach a target SNR (inverse solve).
        ///
        /// Returns a large sentinel time if the target is unreachable (e.g. no
        /// signal). Solves the quadratic form of the radiometer equation for t.
        [[nodiscard]] static f64 time_to_reach_snr(f64 target_snr, const SNRParameters& params);

    private:
        /// Source count rate S = flux * area * efficiency * bandwidth.
        [[nodiscard]] static f64 signal_rate(const SNRParameters& params);
    };
}
