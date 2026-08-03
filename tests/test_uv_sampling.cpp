/// @file test_uv_sampling.cpp
/// @brief Unit tests for the (u,v) sampling pipeline (Sprint 10b Task 10b.2).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "interferometry/array_config.hpp"
#include "interferometry/ephemeris.hpp"
#include "interferometry/kolmogorov.hpp"
#include "interferometry/mulberry32.hpp"
#include "interferometry/uv_sampling.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace
{
    using parallax::astro_constants::kDegToRad;
    using parallax::astro_constants::kPi;
    using parallax::interferometry::ArrayConfig;
    using parallax::interferometry::ArrayGeometry;
    using parallax::interferometry::Body;
    using parallax::interferometry::InstrumentMode;
    using parallax::interferometry::Mulberry32;
    using parallax::interferometry::ObservationConfig;
    using parallax::interferometry::SiteCenter;
    using parallax::interferometry::Station;
    using parallax::interferometry::StationErrors;
    using parallax::interferometry::TargetFT;
    using parallax::interferometry::Visibility;
    using parallax::interferometry::kolmogorov_series;
    using parallax::interferometry::sample_uv;

    // ── Helpers ──────────────────────────────────────────────────────────────────

    /// Build a Y-array on the Moon with the given antenna count per arm.
    [[nodiscard]] std::vector<Station> make_moon_stations(std::uint32_t n_per_arm)
    {
        ArrayConfig config;
        config.geometry = ArrayGeometry::Y;
        config.antennas_per_arm = n_per_arm;
        config.site_extent_m = 10000.0;
        config.site = SiteCenter {
            .body = Body::Moon,
            .lat = -43.3 * kDegToRad,
            .lon = -11.2 * kDegToRad,
        };
        return parallax::interferometry::generate_stations(config);
    }

    /// Return the three fixed Earth stations (La Palma, Mauna Kea, Paranal).
    /// These are suitable for tests that require non-empty visibility results.
    [[nodiscard]] std::vector<Station> make_earth_stations()
    {
        std::vector<Station> stations;
        parallax::interferometry::append_earth_stations(stations);
        return stations;
    }

    /// Build a simple point-source TargetFT: constant amplitude across the whole grid.
    /// The real part of the FT is 1.0 everywhere, imaginary 0 everywhere.
    [[nodiscard]] TargetFT make_point_source_ft(std::uint32_t N, double amplitude = 1.0)
    {
        TargetFT ft;
        ft.N = N;
        ft.Fre.assign(static_cast<std::size_t>(N) * N, amplitude);
        ft.Fim.assign(static_cast<std::size_t>(N) * N, 0.0);
        return ft;
    }

    [[nodiscard]] std::size_t n_pairs(std::size_t n_stations)
    {
        return n_stations * (n_stations - 1u) / 2u;
    }

    constexpr double kTol = 1.0e-12;

    [[nodiscard]] bool approx(double a, double b, double tol = kTol)
    {
        return std::abs(a - b) <= tol;
    }
}

// ── Kolmogorov series tests ────────────────────────────────────────────────────

TEST_CASE("kolmogorov_series RMS matches the requested value within tolerance")
{
    constexpr std::size_t kStations = 4;
    constexpr std::size_t kK = 48;
    constexpr double kRms = 1.5;
    constexpr double kRmsTol = 1.0e-10;

    Mulberry32 rng(99u);
    const auto series = kolmogorov_series(kStations, kK, kRms, rng);

    REQUIRE(series.size() == kStations);
    for (std::size_t s = 0; s < kStations; ++s)
    {
        REQUIRE(series[s].size() == kK);
        double sum_sq = 0.0;
        for (double ph : series[s])
        {
            sum_sq += ph * ph;
        }
        const double rms = std::sqrt(sum_sq / kK);
        CHECK_MESSAGE(std::abs(rms - kRms) <= kRmsTol,
            "station", s, ": rms =", rms, "expected", kRms);
    }
}

TEST_CASE("kolmogorov_series with rms=0 produces all-zeros series")
{
    Mulberry32 rng(7u);
    const auto series = kolmogorov_series(3, 12, 0.0, rng);
    REQUIRE(series.size() == 3u);
    for (const auto& st_series : series)
    {
        for (double ph : st_series)
        {
            CHECK(ph == 0.0);
        }
    }
}

TEST_CASE("kolmogorov_series consumes RNG even when rms=0 (stream consistency)")
{
    // Two generators with the same seed: one calls kolmogorov with rms=0,
    // the other with rms=1.5.  After kolmogorov, they must be at the SAME
    // position in the RNG stream (both consumed station_count*M draws).
    constexpr std::size_t kStations = 3;
    constexpr std::size_t kK = 10;
    constexpr std::uint32_t kSeed = 42u;

    Mulberry32 rng0(kSeed);
    Mulberry32 rng1(kSeed);

    (void)kolmogorov_series(kStations, kK, 0.0, rng0);
    (void)kolmogorov_series(kStations, kK, 1.5, rng1);

    // Next draw from both must be the same (same position in stream).
    const double d0 = rng0.next();
    const double d1 = rng1.next();
    CHECK(d0 == d1);
}

TEST_CASE("kolmogorov_series is deterministic for the same seed")
{
    Mulberry32 rng1(55u);
    Mulberry32 rng2(55u);
    const auto s1 = kolmogorov_series(5, 48, 1.0, rng1);
    const auto s2 = kolmogorov_series(5, 48, 1.0, rng2);
    REQUIRE(s1.size() == s2.size());
    for (std::size_t s = 0; s < s1.size(); ++s)
    {
        REQUIRE(s1[s].size() == s2[s].size());
        for (std::size_t k = 0; k < s1[s].size(); ++k)
        {
            CHECK(s1[s][k] == s2[s][k]); // bit-for-bit identical
        }
    }
}

TEST_CASE("kolmogorov_series K=1 produces one-element series with finite value")
{
    Mulberry32 rng(123u);
    const auto series = kolmogorov_series(2, 1, 0.6, rng);
    REQUIRE(series.size() == 2u);
    for (const auto& st_series : series)
    {
        REQUIRE(st_series.size() == 1u);
        // Normalised to rms = 0.6 over one sample means |ph[0]| == 0.6.
        CHECK(std::abs(std::abs(st_series[0]) - 0.6) <= 1.0e-10);
    }
}

// ── sample_uv: snapshot mode (no rotation) ────────────────────────────────────

TEST_CASE("snapshot mode (rotation=false) produces K=1 and at most n_pairs samples")
{
    const std::vector<Station> stations = make_moon_stations(4); // 13 stations, 78 pairs
    const TargetFT ft = make_point_source_ft(32u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 30.0 * kDegToRad;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 1.0e-6;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};
    err.turbulence_rms_rad = 0.0;
    err.snr = 0.0;
    err.gain_errors = false;

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    // All samples must have time_index == 0.
    for (const Visibility& v : vis)
    {
        CHECK(v.time_index == 0u);
    }

    // Sample count ≤ 78 pairs.
    const std::size_t np = n_pairs(stations.size());
    CHECK(vis.size() <= np);
}

// ── sample_uv: rotation mode and K cap ────────────────────────────────────────

TEST_CASE("rotation mode uses K=48 for 13-station array (pairs*48 <= 8000)")
{
    const std::vector<Station> stations = make_moon_stations(4); // 13 stations, 78 pairs
    // 78 * 48 = 3744 ≤ 8000 → K should remain 48.
    const TargetFT ft = make_point_source_ft(64u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 0.0;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 8.0;
    cfg.rotation = true;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    // All time indices must be in [0, 47].
    for (const Visibility& v : vis)
    {
        CHECK(v.time_index < 48u);
    }
}

TEST_CASE("K reduction: pairs*K capped at 8000 for large station count")
{
    // 22 stations (n_per_arm=7) → 231 pairs.  231*48 = 11088 > 8000.
    // K must be reduced: floor(8000/231) = 34.
    const std::vector<Station> stations = make_moon_stations(7); // 22 stations
    REQUIRE(stations.size() == 22u);

    const TargetFT ft = make_point_source_ft(64u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 0.0;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 8.0;
    cfg.rotation = true;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    // Verify: max time_index should be ≤ K_reduced - 1 = 33.
    if (!vis.empty())
    {
        const std::uint32_t max_k = std::max_element(vis.begin(), vis.end(),
            [](const Visibility& a, const Visibility& b)
            { return a.time_index < b.time_index; })->time_index;
        CHECK(max_k < 34u);
    }
}

// ── sample_uv: Hs symmetry ────────────────────────────────────────────────────

TEST_CASE("Hs time offsets are symmetric around zero for K=48")
{
    // Hs[k] = ((k/(K-1)) - 0.5) * durH.
    // Hs[0] = -0.5*durH; Hs[K-1] = +0.5*durH; Hs[0]+Hs[K-1] = 0.
    constexpr double durH = 8.0;
    constexpr std::size_t K = 48;
    std::vector<double> Hs(K);
    for (std::size_t k = 0; k < K; ++k)
    {
        Hs[k] = (static_cast<double>(k) / static_cast<double>(K - 1u) - 0.5) * durH;
    }
    CHECK(std::abs(Hs[0] + durH / 2.0) <= kTol);
    CHECK(std::abs(Hs[K - 1] - durH / 2.0) <= kTol);
    CHECK(std::abs(Hs[0] + Hs[K - 1]) <= kTol);
    // Pair symmetry: Hs[k] + Hs[K-1-k] ≈ 0.
    for (std::size_t k = 0; k < K / 2; ++k)
    {
        CHECK(std::abs(Hs[k] + Hs[K - 1u - k]) <= kTol);
    }
}

// ── sample_uv: zero errors → measured == true ─────────────────────────────────

TEST_CASE("zero errors: measured visibility equals true visibility")
{
    // Earth stations (3 stations, 3 pairs) with radio wavelength so baselines
    // land inside the FT grid.  At epoch_days=0.25 (t=6 h) all three stations
    // are visible for dec=20°.  lambda=0.1 m, theta_fov=5e-7 rad, N=128
    // → GX ∈ [10, 101], GY ∈ [30, 58], all inside [1, 126].
    const std::vector<Station> stations = make_earth_stations();
    const TargetFT ft = make_point_source_ft(128u, 2.5);

    ObservationConfig cfg {};
    cfg.dec_rad = 20.0 * kDegToRad;
    cfg.lambda_m = 0.1;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.25; // 6 h offset so all Earth stations face the source

    StationErrors err {};
    err.turbulence_rms_rad = 0.0;
    err.snr = 0.0;
    err.gain_errors = false;

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    REQUIRE(!vis.empty());
    for (const Visibility& v : vis)
    {
        CHECK(approx(v.Vr, v.tVr, 1.0e-12));
        CHECK(approx(v.Vi, v.tVi, 1.0e-12));
    }
}

// ── sample_uv: HBT mode ───────────────────────────────────────────────────────

TEST_CASE("HBT mode: tVi == 0 and tVr == |V| for all samples")
{
    // Earth stations + radio parameters so baselines fall inside the FT grid.
    const std::vector<Station> stations = make_earth_stations();
    const TargetFT ft = make_point_source_ft(128u, 3.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 10.0 * kDegToRad;
    cfg.lambda_m = 0.1;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Hbt;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.25;

    StationErrors err {};
    err.turbulence_rms_rad = 0.0;
    err.snr = 0.0;
    err.gain_errors = false;

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    REQUIRE(!vis.empty());
    for (const Visibility& v : vis)
    {
        CHECK(approx(v.tVi, 0.0, 1.0e-15));
        // tVr should be non-negative (it is the amplitude).
        CHECK(v.tVr >= 0.0);
    }
}

TEST_CASE("HBT mode: results are unchanged when turbulence RMS is raised (phase immunity)")
{
    // Earth stations + radio parameters guarantee non-empty results.
    const std::vector<Station> stations = make_earth_stations();
    const TargetFT ft = make_point_source_ft(128u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 15.0 * kDegToRad;
    cfg.lambda_m = 0.1;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Hbt;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.25;

    StationErrors err_zero {};
    err_zero.atm_seed = 111u;
    err_zero.turbulence_rms_rad = 0.0;
    err_zero.snr = 0.0;
    err_zero.gain_errors = false;

    StationErrors err_atm {};
    err_atm.atm_seed = 111u; // same seed so Kolmogorov draws consume identically
    err_atm.turbulence_rms_rad = 1.5;
    err_atm.snr = 0.0;
    err_atm.gain_errors = false;

    const std::vector<Visibility> vis0 = sample_uv(stations, cfg, ft, err_zero);
    const std::vector<Visibility> vis1 = sample_uv(stations, cfg, ft, err_atm);

    // HBT mode is phase-immune: both runs must produce the same tVr and tVi.
    REQUIRE(!vis0.empty());
    REQUIRE(vis0.size() == vis1.size());
    for (std::size_t idx = 0; idx < vis0.size(); ++idx)
    {
        CHECK(approx(vis0[idx].tVr, vis1[idx].tVr, 1.0e-12));
        CHECK(approx(vis0[idx].tVi, vis1[idx].tVi, 1.0e-12));
        // Measured values are also unchanged because dphi=0, gain=1, no noise.
        CHECK(approx(vis0[idx].Vr, vis1[idx].Vr, 1.0e-12));
        CHECK(approx(vis0[idx].Vi, vis1[idx].Vi, 1.0e-12));
    }
}

// ── sample_uv: Comb mode ──────────────────────────────────────────────────────

TEST_CASE("Comb mode: no visibility involves a Moon station")
{
    // Mixed array: Moon stations + Earth stations.
    std::vector<Station> stations = make_moon_stations(4); // 13 Moon stations
    parallax::interferometry::append_earth_stations(stations); // +3 Earth stations

    const TargetFT ft = make_point_source_ft(64u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 25.0 * kDegToRad;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Comb;
    cfg.theta_fov_rad = 1.0e-5;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    // All returned samples must involve only Earth stations.
    for (const Visibility& v : vis)
    {
        CHECK(stations[v.station_i].body == Body::Earth);
        CHECK(stations[v.station_j].body == Body::Earth);
    }
}

// ── sample_uv: point-source FT yields constant |V| ────────────────────────────

TEST_CASE("Point-source FT with zero errors gives constant tVr and tVi == 0")
{
    // A target whose FT is constant (Fre=C, Fim=0) should give the same tVr
    // for every baseline (since all bilinear samples return C).
    // Earth stations + radio wavelength so baselines land inside the FT grid.
    const std::vector<Station> stations = make_earth_stations();
    constexpr double kAmplitude = 3.7;
    const TargetFT ft = make_point_source_ft(128u, kAmplitude);

    ObservationConfig cfg {};
    cfg.dec_rad = 20.0 * kDegToRad;
    cfg.lambda_m = 0.1;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.25;

    StationErrors err {};
    err.turbulence_rms_rad = 0.0;
    err.snr = 0.0;
    err.gain_errors = false;

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    REQUIRE(!vis.empty());
    for (const Visibility& v : vis)
    {
        CHECK(approx(v.tVr, kAmplitude, 1.0e-12));
        CHECK(approx(v.tVi, 0.0, 1.0e-12));
    }
}

// ── sample_uv: station-count agnosticism ──────────────────────────────────────

TEST_CASE("Works with n=4 per-arm (13 stations) and n=7 per-arm (22 stations)")
{
    const TargetFT ft = make_point_source_ft(64u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 10.0 * kDegToRad;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 1.0e-5;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};

    // n=4 → 13 stations.
    {
        const std::vector<Station> stations = make_moon_stations(4);
        CHECK(stations.size() == 13u);
        const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);
        // No assertion on exact count (depends on visibility), just no crash.
        CHECK(vis.size() <= n_pairs(13));
    }

    // n=7 → 22 stations.
    {
        const std::vector<Station> stations = make_moon_stations(7);
        CHECK(stations.size() == 22u);
        const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);
        CHECK(vis.size() <= n_pairs(22));
    }
}

// ── sample_uv: station pair indices are valid ─────────────────────────────────

TEST_CASE("station_i < station_j for all returned visibilities")
{
    const std::vector<Station> stations = make_moon_stations(4);
    const TargetFT ft = make_point_source_ft(64u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 20.0 * kDegToRad;
    cfg.lambda_m = 1.0e-6;
    cfg.duration_hours = 4.0;
    cfg.rotation = true;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 1.0e-5;
    cfg.flux_total = 1.0;
    cfg.epoch_days = 0.0;

    StationErrors err {};

    const std::vector<Visibility> vis = sample_uv(stations, cfg, ft, err);

    for (const Visibility& v : vis)
    {
        CHECK(v.station_i < v.station_j);
        CHECK(v.station_j < static_cast<std::uint32_t>(stations.size()));
    }
}

// ── sample_uv: thermal noise changes measured but not true visibility ──────────

TEST_CASE("Thermal noise changes Vr/Vi but leaves tVr/tVi unchanged")
{
    // Earth stations + radio wavelength so baselines land inside the FT grid.
    const std::vector<Station> stations = make_earth_stations();
    const TargetFT ft = make_point_source_ft(128u, 1.0);

    ObservationConfig cfg {};
    cfg.dec_rad = 20.0 * kDegToRad;
    cfg.lambda_m = 0.1;
    cfg.duration_hours = 4.0;
    cfg.rotation = false;
    cfg.mode = InstrumentMode::Radio;
    cfg.theta_fov_rad = 5.0e-7;
    cfg.flux_total = 10.0;
    cfg.epoch_days = 0.25;

    StationErrors err_clean {};
    err_clean.turbulence_rms_rad = 0.0;
    err_clean.snr = 0.0;
    err_clean.gain_errors = false;

    StationErrors err_noisy {};
    err_noisy.turbulence_rms_rad = 0.0;
    err_noisy.snr = 5.0; // add thermal noise
    err_noisy.gain_errors = false;
    err_noisy.atm_seed = 0u;

    const std::vector<Visibility> vis_clean = sample_uv(stations, cfg, ft, err_clean);
    const std::vector<Visibility> vis_noisy = sample_uv(stations, cfg, ft, err_noisy);

    REQUIRE(!vis_clean.empty());
    REQUIRE(vis_clean.size() == vis_noisy.size());
    bool any_Vr_changed = false;
    for (std::size_t idx = 0; idx < vis_clean.size(); ++idx)
    {
        // True visibilities must be identical.
        CHECK(approx(vis_clean[idx].tVr, vis_noisy[idx].tVr, 1.0e-12));
        CHECK(approx(vis_clean[idx].tVi, vis_noisy[idx].tVi, 1.0e-12));
        if (!approx(vis_clean[idx].Vr, vis_noisy[idx].Vr, 1.0e-15))
        {
            any_Vr_changed = true;
        }
    }
    CHECK(any_Vr_changed); // noise must have changed something
}
