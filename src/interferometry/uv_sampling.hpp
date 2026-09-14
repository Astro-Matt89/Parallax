#pragma once

#include "interferometry/ephemeris.hpp"

#include <cstdint>
#include <vector>

namespace parallax::interferometry
{
    // ── Instrument modes (mirrors sandbox) ───────────────────────────────────────
    enum class InstrumentMode
    {
        Radio, // Direct interferometry (complex visibility), all baselines.
        Comb,  // Optical combiner: Earth stations only (list reduced before pairing).
        Hbt,   // Hanbury Brown–Twiss intensity interferometry: Earth stations only,
               // amplitude only, phase-immune (dphi forced to 0 regardless of turbulence).
        Epr,   // Entanglement-based Earth-Moon optical coherence (complex, like Radio).
    };

    // ── Fourier transform of the target sky image ─────────────────────────────────
    /// Flat N×N grid in row-major order: element (ix, iy) = data[iy * N + ix].
    struct TargetFT
    {
        std::vector<double> Fre; ///< Real part of the 2-D FFT.
        std::vector<double> Fim; ///< Imaginary part of the 2-D FFT.
        std::uint32_t N;         ///< Grid side length (pixels).
        double flux_total = 0.0; ///< Total source flux (sum of sky image; used for noise scaling).
    };

    // ── Observation configuration ─────────────────────────────────────────────────
    struct ObservationConfig
    {
        double dec_rad;         ///< Target declination (radians).
        double lambda_m;        ///< Observing wavelength (metres).
        double duration_hours;  ///< Total observation duration (hours).
        bool rotation;          ///< Enable Earth-rotation aperture synthesis.
        InstrumentMode mode;    ///< Instrument / baseline selection mode.
        double theta_fov_rad;   ///< Field-of-view half-angle used for uv-grid (radians).
        double flux_total;      ///< Total source flux (used for thermal-noise scaling).
        // No epoch here: as in the oracle, the epoch only evolves the target model
        // (render_target_at); every track is centred on t = 0 for the ephemerides.
    };

    // ── Per-station error model ───────────────────────────────────────────────────
    struct StationErrors
    {
        double turbulence_rms_rad = 0.0; ///< Atmospheric phase RMS per station (radians).
        double snr = 0.0;                ///< System SNR; ≤ 0 disables thermal noise.
        bool gain_errors = false;        ///< Enable random gain errors per station.
        std::uint32_t atm_seed = 0;      ///< Seed for the error RNG (used as is, like the oracle).
    };

    // ── Measured visibility sample ────────────────────────────────────────────────
    struct Visibility
    {
        double u;            ///< u coordinate (wavelengths).
        double v;            ///< v coordinate (wavelengths).
        double Vr;           ///< Measured real part (corrupted).
        double Vi;           ///< Measured imaginary part (corrupted).
        double tVr;          ///< True real part (noiseless).
        double tVi;          ///< True imaginary part (noiseless).
        std::uint32_t time_index;  ///< Time-sample index k within the observation.
        std::uint32_t station_i;   ///< Index into the caller's station list of the first station (i < j).
        std::uint32_t station_j;   ///< Index into the caller's station list of the second station.
    };

    /// Sample the (u,v) plane for a given station set, observation, and error model.
    ///
    /// Algorithm summary:
    ///   0. Hbt and Comb keep Earth stations only. As in the oracle the list is REDUCED first,
    ///      so pairs, the K cap and per-station error draws use the reduced list; the
    ///      station indices of the output still refer to `stations`.
    ///   1. Determine K time samples (48 if rotation, 1 otherwise); reduce K when
    ///      pairs × K > 8000 via K = floor(8000 / pairs).
    ///   2. Build sample times: Hs[k] = ((k / (K-1)) - 0.5) × durationHours; Hs[0]=0 when K=1.
    ///   3. Source direction: s3 = {cos(dec), 0, sin(dec)};
    ///      uv basis: eU = {0,-1,0}, eV = {sin(dec), 0, -cos(dec)}.
    ///   4. Enumerate time k (outer) and pairs i < j in index order (inner): the oracle
    ///      order, part of the fixture contract (it fixes noise draws and output order).
    ///   5. For each time k and pair:
    ///        - Compute station states from ephemerides at t = Hs[k] (no epoch offset).
    ///        - Require both stations visible (elevation ≥ sin(EL_MIN), not occulted);
    ///          the oracle hides a station only when up·s < sin(EL_MIN).
    ///        - Baseline B = P_i - P_j; u = B·eU/λ; v = B·eV/λ.
    ///        - Grid coords: du = 1/theta_fov_rad; GX = N/2 + u/du; GY = N/2 + v/du.
    ///        - Bilinear sample target_ft at (GX,GY) if GX,GY ∈ [1, N-2]; else skip.
    ///        - Apply instrument mode (Hbt: amplitude only, dphi = 0).
    ///        - Apply station errors in this order (error-RNG seeded with atm_seed, no xor):
    ///            (a) Kolmogorov phases drawn before the loop (station-major, mode-major);
    ///            (b) gain errors drawn before the loop (per station, if enabled);
    ///            (c) thermal noise drawn inside the loop per visible sample.
    ///   6. Returns all accepted Visibility samples.
    [[nodiscard]] std::vector<Visibility> sample_uv(
        const std::vector<Station>& stations,
        const ObservationConfig& config,
        const TargetFT& target_ft,
        const StationErrors& errors);
}
