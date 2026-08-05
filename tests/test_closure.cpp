/// @file test_closure.cpp
/// @brief Unit tests for closure-phase computation (Sprint 10b Task 10b.5).
///
/// Tests use oracle-independent invariants (SPECIFICA §6 level 5) rather than
/// golden matrices, since closure phase is a derived observable whose absolute
/// value depends on the target model.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "interferometry/array_config.hpp"
#include "interferometry/closure.hpp"
#include "interferometry/ephemeris.hpp"
#include "interferometry/uv_sampling.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace
{

using namespace parallax::interferometry;
using parallax::astro_constants::kDegToRad;
using parallax::astro_constants::kPi;

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Build a Y-array on the Moon.
[[nodiscard]] std::vector<Station> make_moon_stations(std::uint32_t n_per_arm)
{
    ArrayConfig cfg;
    cfg.geometry         = ArrayGeometry::Y;
    cfg.antennas_per_arm = n_per_arm;
    cfg.site_extent_m    = 10000.0;
    cfg.site             = SiteCenter {
        .body = Body::Moon,
        .lat  = -43.3 * kDegToRad,
        .lon  = -11.2 * kDegToRad,
    };
    return generate_stations(cfg);
}

/// Flat-spectrum TargetFT (point source): Fre = amplitude, Fim = 0.
[[nodiscard]] TargetFT make_point_source_ft(std::uint32_t N, double amplitude = 1.0)
{
    TargetFT ft;
    ft.N = N;
    ft.Fre.assign(static_cast<std::size_t>(N) * N, amplitude);
    ft.Fim.assign(static_cast<std::size_t>(N) * N, 0.0);
    return ft;
}

/// Observation config for a transit at declination 20° with Earth rotation.
[[nodiscard]] ObservationConfig make_obs(double lambda_m = 0.5e-6)
{
    return ObservationConfig {
        .dec_rad        = 20.0 * kDegToRad,
        .lambda_m       = lambda_m,
        .duration_hours = 4.0,
        .rotation       = true,
        .mode           = InstrumentMode::Radio,
        .theta_fov_rad  = 1e-6,
        .flux_total     = 1.0,
        .epoch_days     = 0.0,
    };
}

/// Maximum absolute difference between two closure-phase values after wrapping.
double max_closure_diff(
    const std::vector<ClosureTriangle>& a,
    const std::vector<ClosureTriangle>& b)
{
    double max_diff = 0.0;
    // Compare triangle by triangle, sample by sample.
    const std::size_t n_tri = std::min(a.size(), b.size());
    for (std::size_t t = 0; t < n_tri; ++t)
    {
        const std::size_t n_smp = std::min(a[t].samples.size(), b[t].samples.size());
        for (std::size_t s = 0; s < n_smp; ++s)
        {
            const double diff = std::abs(
                wrap_phase(a[t].samples[s].observed - b[t].samples[s].observed));
            max_diff = std::max(max_diff, diff);
        }
    }
    return max_diff;
}

// ── Test 1: Phase-error immunity (SPECIFICA §6 level 5) ──────────────────────

TEST_CASE("Closure phase is immune to station-based turbulence (< 1e-9 rad)")
{
    const auto stations = make_moon_stations(4); // 13 stations
    const auto obs      = make_obs();
    const auto ft       = make_point_source_ft(64);

    StationErrors clean_err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 42 };
    StationErrors turb_err  { .turbulence_rms_rad = 3.0, .snr = 0.0, .gain_errors = false, .atm_seed = 42 };

    const auto vis_clean = sample_uv(stations, obs, ft, clean_err);
    const auto vis_turb  = sample_uv(stations, obs, ft, turb_err);

    REQUIRE_FALSE(vis_clean.empty());
    REQUIRE_FALSE(vis_turb.empty());

    const auto tri_clean = compute_closure_phases(vis_clean, stations);
    const auto tri_turb  = compute_closure_phases(vis_turb,  stations);

    REQUIRE_FALSE(tri_clean.empty());
    REQUIRE_FALSE(tri_turb.empty());
    REQUIRE(tri_clean.size() == tri_turb.size());

    // Guard: individual baseline phases MUST have changed due to turbulence.
    bool any_phase_changed = false;
    for (std::size_t k = 0; k < vis_clean.size() && k < vis_turb.size(); ++k)
    {
        const double phi_c = std::atan2(vis_clean[k].Vi, vis_clean[k].Vr);
        const double phi_t = std::atan2(vis_turb[k].Vi,  vis_turb[k].Vr);
        if (std::abs(wrap_phase(phi_t - phi_c)) > 0.01)
        {
            any_phase_changed = true;
            break;
        }
    }
    CHECK(any_phase_changed); // turbulence was actually applied

    // Main assertion: closure is unchanged.
    const double max_diff = max_closure_diff(tri_clean, tri_turb);
    CHECK(max_diff < 1e-9);
}

// ── Test 2: Gain-error immunity ───────────────────────────────────────────────

TEST_CASE("Closure phase is immune to gain errors (< 1e-9 rad)")
{
    const auto stations = make_moon_stations(4);
    const auto obs      = make_obs();
    const auto ft       = make_point_source_ft(64);

    StationErrors clean_err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 7 };
    StationErrors gain_err  { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = true,  .atm_seed = 7 };

    const auto vis_clean = sample_uv(stations, obs, ft, clean_err);
    const auto vis_gain  = sample_uv(stations, obs, ft, gain_err);

    REQUIRE_FALSE(vis_clean.empty());
    REQUIRE_FALSE(vis_gain.empty());

    const auto tri_clean = compute_closure_phases(vis_clean, stations);
    const auto tri_gain  = compute_closure_phases(vis_gain,  stations);

    REQUIRE_FALSE(tri_clean.empty());
    REQUIRE_FALSE(tri_gain.empty());
    REQUIRE(tri_clean.size() == tri_gain.size());

    const double max_diff = max_closure_diff(tri_clean, tri_gain);
    CHECK(max_diff < 1e-9);
}

// ── Test 3: Thermal noise DOES break immunity ─────────────────────────────────

TEST_CASE("Thermal noise corrupts closure (non-station-based error)")
{
    const auto stations = make_moon_stations(4);
    const auto obs      = make_obs();
    const auto ft       = make_point_source_ft(64, 1.0);

    // Strong noise: SNR = 2 → noise is comparable to signal.
    StationErrors clean_err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 99 };
    StationErrors noise_err { .turbulence_rms_rad = 0.0, .snr = 2.0, .gain_errors = false, .atm_seed = 99 };

    const auto vis_clean = sample_uv(stations, obs, ft, clean_err);
    const auto vis_noise = sample_uv(stations, obs, ft, noise_err);

    REQUIRE_FALSE(vis_clean.empty());
    REQUIRE_FALSE(vis_noise.empty());

    const auto tri_clean = compute_closure_phases(vis_clean, stations);
    const auto tri_noise = compute_closure_phases(vis_noise,  stations);

    REQUIRE_FALSE(tri_clean.empty());
    REQUIRE_FALSE(tri_noise.empty());

    // The closure phases should differ due to thermal noise.
    const double max_diff = max_closure_diff(tri_clean, tri_noise);
    CHECK(max_diff > 1e-6); // noise is non-zero
    CHECK(max_diff < kPi);  // but bounded within the phase range
}

// ── Test 4: Analytic hand-built case ─────────────────────────────────────────

TEST_CASE("Analytic case: known phases produce exact closure")
{
    // Three synthetic visibilities for one triangle at time_index = 0.
    // Phases: φ_ab = 0.3, φ_bc = 0.5, φ_ac = 0.7 rad.
    // Expected closure: 0.3 + 0.5 - 0.7 = 0.1 rad.
    const double phi_ab = 0.3;
    const double phi_bc = 0.5;
    const double phi_ac = 0.7;

    std::vector<Visibility> pts;
    pts.push_back(Visibility {
        .u = 0.0, .v = 0.0,
        .Vr = std::cos(phi_ab), .Vi = std::sin(phi_ab),
        .tVr = std::cos(phi_ab), .tVi = std::sin(phi_ab),
        .time_index = 0, .station_i = 0, .station_j = 1,
    });
    pts.push_back(Visibility {
        .u = 0.0, .v = 0.0,
        .Vr = std::cos(phi_bc), .Vi = std::sin(phi_bc),
        .tVr = std::cos(phi_bc), .tVi = std::sin(phi_bc),
        .time_index = 0, .station_i = 1, .station_j = 2,
    });
    pts.push_back(Visibility {
        .u = 0.0, .v = 0.0,
        .Vr = std::cos(phi_ac), .Vi = std::sin(phi_ac),
        .tVr = std::cos(phi_ac), .tVi = std::sin(phi_ac),
        .time_index = 0, .station_i = 0, .station_j = 2,
    });

    std::vector<Station> stations(3);
    const auto result = compute_closure_phases(pts, stations);

    REQUIRE(result.size() == 1u);
    REQUIRE(result[0].samples.size() == 1u);

    constexpr double kTol = 1e-12;
    CHECK(std::abs(result[0].samples[0].observed - (phi_ab + phi_bc - phi_ac)) < kTol);
    CHECK(std::abs(result[0].samples[0].truth    - (phi_ab + phi_bc - phi_ac)) < kTol);
}

// ── Test 5: Phase wrapping ────────────────────────────────────────────────────

TEST_CASE("wrap_phase maps to (-pi, pi]")
{
    constexpr double kTol = 1e-15;

    // Already in range
    CHECK(std::abs(wrap_phase(0.0)) < kTol);
    CHECK(std::abs(wrap_phase(kPi)) < kTol);         // π → π
    CHECK(std::abs(wrap_phase(-kPi) - kPi) < kTol);  // -π → +π (closed at +π)

    // Beyond +π
    CHECK(std::abs(wrap_phase(kPi + 0.1) - (-kPi + 0.1)) < kTol);

    // Far beyond
    CHECK(std::abs(wrap_phase(5.0) - wrap_phase(5.0 - 2.0 * kPi)) < kTol);
}

TEST_CASE("Closure wraps correctly when individual phases sum beyond pi")
{
    // φ_ab = 0.9π, φ_bc = 0.9π, φ_ac = -0.9π
    // Sum = 0.9π + 0.9π - (-0.9π) = 2.7π  → wrapped to 2.7π - 2π = 0.7π
    const double phi_ab =  0.9 * kPi;
    const double phi_bc =  0.9 * kPi;
    const double phi_ac = -0.9 * kPi;

    std::vector<Visibility> pts;
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = std::cos(phi_ab), .Vi = std::sin(phi_ab),
        .tVr = std::cos(phi_ab), .tVi = std::sin(phi_ab),
        .time_index = 0, .station_i = 0, .station_j = 1,
    });
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = std::cos(phi_bc), .Vi = std::sin(phi_bc),
        .tVr = std::cos(phi_bc), .tVi = std::sin(phi_bc),
        .time_index = 0, .station_i = 1, .station_j = 2,
    });
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = std::cos(phi_ac), .Vi = std::sin(phi_ac),
        .tVr = std::cos(phi_ac), .tVi = std::sin(phi_ac),
        .time_index = 0, .station_i = 0, .station_j = 2,
    });

    std::vector<Station> stations(3);
    const auto result = compute_closure_phases(pts, stations);

    REQUIRE(result.size() == 1u);
    REQUIRE(result[0].samples.size() == 1u);

    const double expected = wrap_phase(phi_ab + phi_bc - phi_ac);
    CHECK(std::abs(result[0].samples[0].observed - expected) < 1e-12);
    CHECK(expected > -kPi);
    CHECK(expected <= kPi);
}

// ── Test 6: Triangle selection determinism ────────────────────────────────────

TEST_CASE("Triangle selection is deterministic across repeated calls")
{
    const auto stations = make_moon_stations(4);
    const auto obs      = make_obs();
    const auto ft       = make_point_source_ft(64);
    const StationErrors err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 1 };

    const auto vis = sample_uv(stations, obs, ft, err);
    REQUIRE_FALSE(vis.empty());

    const auto tri1 = compute_closure_phases(vis, stations);
    const auto tri2 = compute_closure_phases(vis, stations);

    REQUIRE(tri1.size() == tri2.size());
    for (std::size_t t = 0; t < tri1.size(); ++t)
    {
        CHECK(tri1[t].a == tri2[t].a);
        CHECK(tri1[t].b == tri2[t].b);
        CHECK(tri1[t].c == tri2[t].c);
        REQUIRE(tri1[t].samples.size() == tri2[t].samples.size());
        for (std::size_t s = 0; s < tri1[t].samples.size(); ++s)
            CHECK(tri1[t].samples[s].time_index == tri2[t].samples[s].time_index);
    }
}

// ── Test 7: Partial coverage ──────────────────────────────────────────────────

TEST_CASE("Samples only emitted at times where all three baselines are present")
{
    // Build a three-station set where baseline (0,1) only has time_index 0,
    // baseline (1,2) has times 0 and 1, and baseline (0,2) has time 0 only.
    // Only time 0 has full coverage.
    std::vector<Visibility> pts;
    // time 0: all three baselines
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = 1, .Vi = 0, .tVr = 1, .tVi = 0,
        .time_index = 0, .station_i = 0, .station_j = 1,
    });
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = 1, .Vi = 0, .tVr = 1, .tVi = 0,
        .time_index = 0, .station_i = 1, .station_j = 2,
    });
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = 1, .Vi = 0, .tVr = 1, .tVi = 0,
        .time_index = 0, .station_i = 0, .station_j = 2,
    });
    // time 1: only (1,2) present
    pts.push_back(Visibility {
        .u = 0, .v = 0,
        .Vr = 1, .Vi = 0, .tVr = 1, .tVi = 0,
        .time_index = 1, .station_i = 1, .station_j = 2,
    });

    std::vector<Station> stations(3);
    const auto result = compute_closure_phases(pts, stations);

    REQUIRE(result.size() == 1u);
    // Only time 0 should appear in the samples.
    REQUIRE(result[0].samples.size() == 1u);
    CHECK(result[0].samples[0].time_index == 0u);
}

// ── Test 8: Degenerate inputs ─────────────────────────────────────────────────

TEST_CASE("Fewer than 3 stations returns empty and does not crash")
{
    std::vector<Station> stations(2);
    std::vector<Visibility> pts;
    pts.push_back(Visibility {
        .u = 0, .v = 0, .Vr = 1, .Vi = 0, .tVr = 1, .tVi = 0,
        .time_index = 0, .station_i = 0, .station_j = 1,
    });
    const auto result = compute_closure_phases(pts, stations);
    CHECK(result.empty());
}

TEST_CASE("Empty visibility list returns empty and does not crash")
{
    std::vector<Station> stations(5);
    std::vector<Visibility> pts;
    const auto result = compute_closure_phases(pts, stations);
    CHECK(result.empty());
}

TEST_CASE("No NaN in result on zero-amplitude visibilities")
{
    // All visibilities have Vr=Vi=0 → atan2(0,0) is implementation-defined
    // but must not propagate NaN into the closure.
    std::vector<Visibility> pts;
    pts.push_back(Visibility { .u=0,.v=0,.Vr=0,.Vi=0,.tVr=0,.tVi=0,.time_index=0,.station_i=0,.station_j=1 });
    pts.push_back(Visibility { .u=0,.v=0,.Vr=0,.Vi=0,.tVr=0,.tVi=0,.time_index=0,.station_i=1,.station_j=2 });
    pts.push_back(Visibility { .u=0,.v=0,.Vr=0,.Vi=0,.tVr=0,.tVi=0,.time_index=0,.station_i=0,.station_j=2 });

    std::vector<Station> stations(3);
    const auto result = compute_closure_phases(pts, stations);
    // Should produce one sample; the phase values (atan2(0,0)) are 0.0 on all platforms.
    if (!result.empty() && !result[0].samples.empty())
    {
        CHECK_FALSE(std::isnan(result[0].samples[0].observed));
        CHECK_FALSE(std::isnan(result[0].samples[0].truth));
    }
}

// ── Test 9: Arbitrary station counts (no hardcoding) ─────────────────────────

TEST_CASE("Works with antennas_per_arm = 4 (13 stations)")
{
    const auto stations = make_moon_stations(4);
    CHECK(stations.size() == 13u);

    const auto obs = make_obs();
    const auto ft  = make_point_source_ft(64);
    const StationErrors err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 0 };

    const auto vis    = sample_uv(stations, obs, ft, err);
    const auto result = compute_closure_phases(vis, stations);

    // Should find triangles and they must be within station index range.
    REQUIRE_FALSE(result.empty());
    for (const auto& tri : result)
    {
        CHECK(tri.a < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.b < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.c < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.a < tri.b);
        CHECK(tri.b < tri.c);
        CHECK_FALSE(tri.samples.empty());
    }
}

TEST_CASE("Works with antennas_per_arm = 7 (22 stations)")
{
    const auto stations = make_moon_stations(7);
    CHECK(stations.size() == 22u);

    const auto obs = make_obs();
    const auto ft  = make_point_source_ft(64);
    const StationErrors err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 0 };

    const auto vis    = sample_uv(stations, obs, ft, err);
    const auto result = compute_closure_phases(vis, stations);

    REQUIRE_FALSE(result.empty());
    for (const auto& tri : result)
    {
        CHECK(tri.a < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.b < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.c < static_cast<std::uint32_t>(stations.size()));
        CHECK(tri.a < tri.b);
        CHECK(tri.b < tri.c);
        CHECK_FALSE(tri.samples.empty());
    }
}

// ── Test 10: max_triangles parameter ─────────────────────────────────────────

TEST_CASE("max_triangles parameter limits the number of returned triangles")
{
    const auto stations = make_moon_stations(4);
    const auto obs      = make_obs();
    const auto ft       = make_point_source_ft(64);
    const StationErrors err { .turbulence_rms_rad = 0.0, .snr = 0.0, .gain_errors = false, .atm_seed = 5 };

    const auto vis = sample_uv(stations, obs, ft, err);
    REQUIRE_FALSE(vis.empty());

    const auto tri1 = compute_closure_phases(vis, stations, 1);
    const auto tri3 = compute_closure_phases(vis, stations, 3);
    const auto tri9 = compute_closure_phases(vis, stations, 9);

    CHECK(tri1.size() <= 1u);
    CHECK(tri3.size() <= 3u);
    CHECK(tri9.size() <= 9u);

    // The first triangle returned by tri1 and tri3 must be identical.
    if (!tri1.empty() && !tri3.empty())
    {
        CHECK(tri1[0].a == tri3[0].a);
        CHECK(tri1[0].b == tri3[0].b);
        CHECK(tri1[0].c == tri3[0].c);
    }
}

} // namespace
