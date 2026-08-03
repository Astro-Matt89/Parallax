#include "interferometry/uv_sampling.hpp"

#include "interferometry/ephemeris.hpp"
#include "interferometry/kolmogorov.hpp"
#include "interferometry/mulberry32.hpp"

#include <cmath>
#include <cstddef>

namespace parallax::interferometry
{
    namespace
    {
        // Maximum total samples before K is reduced (normative constant from sandbox).
        constexpr std::size_t kMaxTotalSamples = 8000;

        // Full number of time steps when rotation is enabled.
        constexpr std::size_t kKRotation = 48;

        /// Build the time-offset vector Hs (hours from the observation centre).
        /// When K == 1, Hs = {0}.  When K > 1, Hs[k] = ((k/(K-1)) - 0.5) × durH.
        [[nodiscard]] std::vector<double> make_time_offsets(std::size_t K, double duration_hours)
        {
            std::vector<double> Hs(K);
            if (K == 1)
            {
                Hs[0] = 0.0;
            }
            else
            {
                const double Km1 = static_cast<double>(K - 1u);
                for (std::size_t k = 0; k < K; ++k)
                {
                    Hs[k] = (static_cast<double>(k) / Km1 - 0.5) * duration_hours;
                }
            }
            return Hs;
        }

        /// Bilinearly sample a flat N×N grid at continuous position (gx, gy).
        /// The caller MUST verify gx and gy are in [1, N-2] before calling.
        [[nodiscard]] std::pair<double, double> bilinear_sample(
            const TargetFT& ft, double gx, double gy)
        {
            const auto N = static_cast<int>(ft.N);
            const int ix0 = static_cast<int>(std::floor(gx));
            const int iy0 = static_cast<int>(std::floor(gy));
            const double fx = gx - static_cast<double>(ix0);
            const double fy = gy - static_cast<double>(iy0);
            const int ix1 = ix0 + 1;
            const int iy1 = iy0 + 1;

            const auto idx = [N](int iy, int ix) { return iy * N + ix; };

            const double r00 = ft.Fre[idx(iy0, ix0)];
            const double r10 = ft.Fre[idx(iy0, ix1)];
            const double r01 = ft.Fre[idx(iy1, ix0)];
            const double r11 = ft.Fre[idx(iy1, ix1)];

            const double i00 = ft.Fim[idx(iy0, ix0)];
            const double i10 = ft.Fim[idx(iy0, ix1)];
            const double i01 = ft.Fim[idx(iy1, ix0)];
            const double i11 = ft.Fim[idx(iy1, ix1)];

            const double tVr = (1.0 - fy) * ((1.0 - fx) * r00 + fx * r10)
                + fy * ((1.0 - fx) * r01 + fx * r11);
            const double tVi = (1.0 - fy) * ((1.0 - fx) * i00 + fx * i10)
                + fy * ((1.0 - fx) * i01 + fx * i11);

            return {tVr, tVi};
        }
    }

    std::vector<Visibility> sample_uv(
        const std::vector<Station>& stations,
        const ObservationConfig& config,
        const TargetFT& target_ft,
        const StationErrors& errors)
    {
        const std::size_t n_stations = stations.size();
        if (n_stations < 2 || target_ft.N < 3 || target_ft.Fre.empty())
        {
            return {};
        }

        // ── 1. Number of time samples K ──────────────────────────────────────────
        const std::size_t n_pairs = n_stations * (n_stations - 1u) / 2u;

        std::size_t K = 1u;
        if (config.rotation)
        {
            K = kKRotation;
            // Cap total samples: if pairs × K > 8000, reduce K deterministically.
            // Rule: K = floor(8000 / pairs), minimum 1.
            if (n_pairs * K > kMaxTotalSamples)
            {
                K = std::max(std::size_t{1u}, kMaxTotalSamples / n_pairs);
            }
        }

        // ── 2. Sample times Hs (hours offset from observation centre) ─────────────
        const std::vector<double> Hs = make_time_offsets(K, config.duration_hours);

        // ── 3. Source direction and uv-plane basis (normative formulas) ───────────
        const double dec = config.dec_rad;
        const Vec3d s3  { std::cos(dec), 0.0, std::sin(dec) };
        const Vec3d eU  { 0.0, -1.0, 0.0 };
        const Vec3d eV  { std::sin(dec), 0.0, -std::cos(dec) };

        const double du   = 1.0 / config.theta_fov_rad;
        const double half_N = static_cast<double>(target_ft.N) / 2.0;
        const double N_m2   = static_cast<double>(target_ft.N) - 2.0;

        // ── 4. Error-RNG (seeded separately per SPECIFICA §2) ────────────────────
        // Seeding contract: errors use atmSeed ^ 0x9e3779b9 (Fibonacci constant).
        Mulberry32 err_rng(errors.atm_seed ^ 0x9e3779b9u);

        // ── 5. Kolmogorov atmospheric phase series (drawn before sampling loop) ──
        // Generation order: station-major, mode-major (see kolmogorov.hpp).
        // Draws happen regardless of rms so the RNG stream stays consistent.
        const std::vector<std::vector<double>> phases = kolmogorov_series(
            n_stations, K, errors.turbulence_rms_rad, err_rng);

        // ── 6. Gain errors per station (drawn before sampling loop) ───────────────
        // gain = max(0.3, 1 + randn * 0.18) when enabled, otherwise 1.
        // Each gain consumes 2 RNG draws (one randn() = 2 next() calls via Box-Muller).
        std::vector<double> gains(n_stations, 1.0);
        if (errors.gain_errors)
        {
            for (std::size_t s = 0; s < n_stations; ++s)
            {
                const double g = 1.0 + err_rng.randn() * 0.18;
                gains[s] = (g < 0.3) ? 0.3 : g;
            }
        }

        // ── 7. Sampling loop: enumerate pairs (i < j) then time (k) ──────────────
        const bool comb_mode = (config.mode == InstrumentMode::Comb);
        const bool hbt_mode  = (config.mode == InstrumentMode::Hbt);
        const double noise_sig = (errors.snr > 0.0) ? (config.flux_total / errors.snr) : 0.0;

        std::vector<Visibility> result;
        result.reserve(n_pairs * K / 2); // rough pre-allocation

        for (std::size_t i = 0; i < n_stations; ++i)
        {
            for (std::size_t j = i + 1u; j < n_stations; ++j)
            {
                // Comb mode: skip pairs involving any Moon station.
                if (comb_mode
                    && (stations[i].body == Body::Moon || stations[j].body == Body::Moon))
                {
                    continue;
                }

                for (std::size_t k = 0; k < K; ++k)
                {
                    // Absolute observation time for this sample.
                    const double t_hours = config.epoch_days * 24.0 + Hs[k];

                    // Station states at time t.
                    const StationState si = station_state(stations[i], t_hours);
                    const StationState sj = station_state(stations[j], t_hours);

                    // Visibility check: elevation ≥ sin(EL_MIN) AND not occulted.
                    // Brief uses ≥; SPECIFICA §3 uses >. Implemented as ≥ here
                    // (matching is_visible); flip to > if a fixture disagrees.
                    if (!is_visible(si, s3, t_hours, stations[i].body)
                        || !is_visible(sj, s3, t_hours, stations[j].body))
                    {
                        continue;
                    }

                    // Baseline and (u,v) coordinates.
                    const Vec3d B = si.position - sj.position;
                    const double u = glm::dot(B, eU) / config.lambda_m;
                    const double v = glm::dot(B, eV) / config.lambda_m;

                    // Grid coordinates for bilinear FT sampling.
                    const double GX = half_N + u / du;
                    const double GY = half_N + v / du;

                    // SPECIFICA §3: discard samples outside [1, N-2].
                    if (GX < 1.0 || GX > N_m2 || GY < 1.0 || GY > N_m2)
                    {
                        continue;
                    }

                    // Bilinear sample of the target's Fourier transform.
                    auto [tVr, tVi] = bilinear_sample(target_ft, GX, GY);

                    // HBT mode: amplitude-only, phase-immune.
                    if (hbt_mode)
                    {
                        tVr = std::hypot(tVr, tVi);
                        tVi = 0.0;
                    }

                    // Atmospheric phase difference (0 for HBT).
                    double dphi = 0.0;
                    if (!hbt_mode && !phases.empty())
                    {
                        dphi = phases[i][k] - phases[j][k];
                    }

                    // Gain product.
                    const double g = gains[i] * gains[j];

                    // Apply corruption: rotation by dphi, then gain.
                    const double cos_dphi = std::cos(dphi);
                    const double sin_dphi = std::sin(dphi);
                    double Vr = g * (tVr * cos_dphi - tVi * sin_dphi);
                    double Vi = g * (tVr * sin_dphi + tVi * cos_dphi);

                    // Thermal noise (one randn() per component = 2 draws each).
                    if (errors.snr > 0.0)
                    {
                        Vr += noise_sig * err_rng.randn();
                        Vi += noise_sig * err_rng.randn();
                    }

                    result.push_back(Visibility {
                        .u = u,
                        .v = v,
                        .Vr = Vr,
                        .Vi = Vi,
                        .tVr = tVr,
                        .tVi = tVi,
                        .time_index = static_cast<std::uint32_t>(k),
                        .station_i = static_cast<std::uint32_t>(i),
                        .station_j = static_cast<std::uint32_t>(j),
                    });
                }
            }
        }

        return result;
    }
}
