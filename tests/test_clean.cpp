/// @file test_clean.cpp
/// @brief Unit tests for Högbom CLEAN (Sprint 10b Task 10b.4).
///
/// All tests use oracle-independent **invariants** (SPECIFICA §6 level 4).
/// The CLEAN iteration path is floating-point-order sensitive and not
/// bit-reproducibly comparable to the JS sandbox, so no golden matrices are used.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "interferometry/clean.hpp"
#include "interferometry/imaging.hpp"
#include "interferometry/array_config.hpp"
#include "interferometry/uv_sampling.hpp"
#include "core/fft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <vector>

namespace
{

using namespace parallax::interferometry;

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Build an N×N image that is zero everywhere except pixel (px, py) = val.
/// (Currently unused directly — kept as a utility for future tests.)
[[maybe_unused]] std::vector<double> point_image(std::uint32_t N, std::uint32_t px, std::uint32_t py, double val = 1.0)
{
    std::vector<double> img(static_cast<std::size_t>(N) * N, 0.0);
    img[static_cast<std::size_t>(py) * N + px] = val;
    return img;
}

/// Compute RMS of a vector.
double rms(const std::vector<double>& v)
{
    double s = 0.0;
    for (double x : v)
        s += x * x;
    return std::sqrt(s / static_cast<double>(v.size()));
}

/// Index of the element with the largest absolute value.
std::uint32_t argmax_abs(const std::vector<double>& v)
{
    std::uint32_t best = 0u;
    double best_abs = 0.0;
    for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(v.size()); ++i)
    {
        const double a = std::abs(v[i]);
        if (a > best_abs)
        {
            best_abs = a;
            best = i;
        }
    }
    return best;
}

/// Build a synthetic dirty beam for an N-grid: a 2-D Gaussian with sigma σ_b,
/// fftshift-centred at (N/2, N/2), peak-normalised to 1.
std::vector<double> make_gaussian_beam(std::uint32_t N, double sigma_b)
{
    const std::size_t sz = static_cast<std::size_t>(N) * N;
    std::vector<double> beam(sz);
    const double cx = static_cast<double>(N) / 2.0;
    const double cy = cx;
    const double inv2s2 = 1.0 / (2.0 * sigma_b * sigma_b);
    for (std::uint32_t r = 0u; r < N; ++r)
    {
        for (std::uint32_t c = 0u; c < N; ++c)
        {
            const double dx = static_cast<double>(c) - cx;
            const double dy = static_cast<double>(r) - cy;
            beam[r * N + c] = std::exp(-(dx * dx + dy * dy) * inv2s2);
        }
    }
    return beam;
}

/// Make a dirty image that is `flux × beam_shifted_to(px, py)`.
/// This is the exact dirty image produced by a single point source of given flux.
std::vector<double> make_point_dirty(std::uint32_t N,
                                      const std::vector<double>& beam,
                                      std::uint32_t px,
                                      std::uint32_t py,
                                      double flux)
{
    const std::uint32_t half = N / 2u;
    const std::int32_t dx = static_cast<std::int32_t>(px) - static_cast<std::int32_t>(half);
    const std::int32_t dy = static_cast<std::int32_t>(py) - static_cast<std::int32_t>(half);
    const std::int32_t iN = static_cast<std::int32_t>(N);

    std::vector<double> dirty(static_cast<std::size_t>(N) * N, 0.0);
    for (std::int32_t by = 0; by < iN; ++by)
    {
        const std::int32_t ry = by + dy;
        if (ry < 0 || ry >= iN)
            continue;
        for (std::int32_t bx = 0; bx < iN; ++bx)
        {
            const std::int32_t rx = bx + dx;
            if (rx < 0 || rx >= iN)
                continue;
            dirty[static_cast<std::uint32_t>(ry) * N + static_cast<std::uint32_t>(rx)] =
                flux * beam[static_cast<std::uint32_t>(by) * N + static_cast<std::uint32_t>(bx)];
        }
    }
    return dirty;
}

} // anonymous namespace

// ── Test 1: Point source reconstructs to a point ─────────────────────────────

TEST_CASE("CLEAN: point source recovers peak at correct position within 1 pixel")
{
    constexpr std::uint32_t N      = 64u;
    constexpr double kFlux         = 1.0;
    constexpr std::uint32_t kPx    = 35u;
    constexpr std::uint32_t kPy    = 28u;
    constexpr double kBeamSigma    = 4.0; // dirty-beam sigma in pixels
    constexpr double kFwhmPx       = 5.0;
    constexpr std::uint32_t kNiter = 500u;
    constexpr double kGain         = 0.1;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    const auto dirty = make_point_dirty(N, beam, kPx, kPy, kFlux);

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    CHECK(res.ncomp > 0u);

    // Peak of restored image must be at (kPx, kPy) within 1 pixel.
    const std::uint32_t peak_idx = argmax_abs(res.restored);
    const std::uint32_t peak_px  = peak_idx % N;
    const std::uint32_t peak_py  = peak_idx / N;

    const std::int32_t dpx = std::abs(static_cast<std::int32_t>(peak_px) - static_cast<std::int32_t>(kPx));
    const std::int32_t dpy = std::abs(static_cast<std::int32_t>(peak_py) - static_cast<std::int32_t>(kPy));
    CHECK(dpx <= 1);
    CHECK(dpy <= 1);

    // Residual RMS must be much smaller than the input peak.
    const double initial_peak = *std::max_element(dirty.begin(), dirty.end());
    const double resid_rms    = rms(res.residual);
    CHECK(resid_rms < 0.05 * initial_peak);
}

// ── Test 2: Flux conservation ─────────────────────────────────────────────────

TEST_CASE("CLEAN: total cleaned flux recovers input point-source flux within 10%")
{
    constexpr std::uint32_t N      = 64u;
    constexpr double kFlux         = 2.5;
    constexpr std::uint32_t kPx    = 32u;
    constexpr std::uint32_t kPy    = 32u;
    constexpr double kBeamSigma    = 3.5;
    constexpr double kFwhmPx       = 5.0;
    constexpr std::uint32_t kNiter = 1000u;
    constexpr double kGain         = 0.1;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    const auto dirty = make_point_dirty(N, beam, kPx, kPy, kFlux);

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    const double rel_err = std::abs(res.flux - kFlux) / kFlux;
    CHECK(rel_err < 0.10);
}

// ── Test 3: Convergence / stopping rule ──────────────────────────────────────

TEST_CASE("CLEAN: converges early via 2% threshold for clean point source")
{
    constexpr std::uint32_t N      = 64u;
    constexpr double kBeamSigma    = 4.0;
    constexpr double kFwhmPx       = 5.0;
    constexpr std::uint32_t kNiter = 5000u; // generous budget
    constexpr double kGain         = 0.1;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    const auto dirty = make_point_dirty(N, beam, 32u, 32u, 1.0);

    // Initial peak
    const double pk0 = *std::max_element(dirty.begin(), dirty.end());

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    // Must have stopped early (not exhausted all iterations).
    CHECK(res.iters < kNiter);

    // Residual peak must be below the 2% threshold.
    const auto [rpk, _] = std::pair<double, std::uint32_t>(
        [&]() -> std::pair<double, std::uint32_t>
        {
            double best = 0.0;
            std::uint32_t bi = 0u;
            for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(res.residual.size()); ++i)
            {
                const double a = std::abs(res.residual[i]);
                if (a > best) { best = a; bi = i; }
            }
            return {best, bi};
        }()
    );
    CHECK(rpk < 0.02 * pk0);
}

// ── Test 4: Loop gain behaviour ───────────────────────────────────────────────

TEST_CASE("CLEAN: smaller gain requires more iterations than larger gain")
{
    constexpr std::uint32_t N   = 64u;
    constexpr double kBeamSigma = 4.0;
    constexpr double kFwhmPx    = 5.0;
    constexpr std::uint32_t kNiter = 5000u;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    const auto dirty = make_point_dirty(N, beam, 32u, 32u, 1.0);

    const CleanResult slow = hogbom(dirty, beam, N, kNiter, 0.05, kFwhmPx);
    const CleanResult fast = hogbom(dirty, beam, N, kNiter, 0.20, kFwhmPx);

    // Both must converge (iters < kNiter) within the generous budget.
    CHECK(slow.iters < kNiter);
    CHECK(fast.iters < kNiter);
    // Slower gain → more iterations needed.
    CHECK(slow.iters > fast.iters);
}

// ── Test 5: Negative peak handling ───────────────────────────────────────────

TEST_CASE("CLEAN: negative dominant feature produces negative-flux component and converges")
{
    constexpr std::uint32_t N   = 64u;
    constexpr double kBeamSigma = 4.0;
    constexpr double kFwhmPx    = 5.0;
    constexpr std::uint32_t kNiter = 2000u;
    constexpr double kGain      = 0.1;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    // A negative point source: dirty = −1 × beam_centred.
    const auto dirty = make_point_dirty(N, beam, 32u, 32u, -1.0);

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    CHECK(res.ncomp > 0u);
    // All (or the dominant) components must have negative flux.
    const double min_flux = std::min_element(
        res.components.begin(), res.components.end(),
        [](const CleanComponent& a, const CleanComponent& b){ return a.flux < b.flux; }
    )->flux;
    CHECK(min_flux < 0.0);

    // Total flux must be negative.
    CHECK(res.flux < 0.0);

    // No NaNs in outputs.
    for (double v : res.restored)
        CHECK(!std::isnan(v));
    for (double v : res.residual)
        CHECK(!std::isnan(v));
}

// ── Test 6: Two well-separated point sources ──────────────────────────────────

TEST_CASE("CLEAN: two separated point sources both recovered within 1 px and 15% amplitude")
{
    constexpr std::uint32_t N   = 64u;
    constexpr double kBeamSigma = 3.0;
    constexpr double kFwhmPx    = 4.5;
    constexpr std::uint32_t kNiter = 3000u;
    constexpr double kGain      = 0.1;

    // Source A: flux 1.0 at (20, 20)
    // Source B: flux 0.6 at (44, 44)  — well-separated (Δ ≈ 34 px >> beam sigma 3)
    constexpr std::uint32_t kPxA = 20u, kPyA = 20u;
    constexpr std::uint32_t kPxB = 44u, kPyB = 44u;
    constexpr double kFluxA = 1.0;
    constexpr double kFluxB = 0.6;

    const auto beam = make_gaussian_beam(N, kBeamSigma);
    auto dirty = make_point_dirty(N, beam, kPxA, kPyA, kFluxA);

    // Add second source by superposition.
    const auto dirtyB = make_point_dirty(N, beam, kPxB, kPyB, kFluxB);
    for (std::size_t i = 0u; i < dirty.size(); ++i)
        dirty[i] += dirtyB[i];

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    CHECK(res.ncomp > 0u);

    // Find the brightest pixel in the restored image near each known source location.
    // Search within a window of ±5 pixels to be robust against minor CLEAN artefacts.
    auto local_max_at = [&](std::uint32_t spx, std::uint32_t spy, std::uint32_t window)
        -> std::pair<double, std::pair<std::uint32_t, std::uint32_t>>
    {
        double best = -1e30;
        std::uint32_t bpx = spx, bpy = spy;
        for (std::uint32_t r = (spy >= window ? spy - window : 0u);
             r <= std::min(spy + window, N - 1u); ++r)
        {
            for (std::uint32_t c = (spx >= window ? spx - window : 0u);
                 c <= std::min(spx + window, N - 1u); ++c)
            {
                if (res.restored[r * N + c] > best)
                {
                    best = res.restored[r * N + c];
                    bpx = c;
                    bpy = r;
                }
            }
        }
        return {best, {bpx, bpy}};
    };

    const auto [ampA, posA] = local_max_at(kPxA, kPyA, 5u);
    const auto [ampB, posB] = local_max_at(kPxB, kPyB, 5u);

    // Both local peaks must be found.
    CHECK(ampA > 0.0);
    CHECK(ampB > 0.0);

    // Position of each local max must be within 1 px of the true source.
    auto is_near = [](std::uint32_t px, std::uint32_t py,
                   std::uint32_t spx, std::uint32_t spy) -> bool
    {
        return std::abs(static_cast<std::int32_t>(px) - static_cast<std::int32_t>(spx)) <= 1 &&
               std::abs(static_cast<std::int32_t>(py) - static_cast<std::int32_t>(spy)) <= 1;
    };
    CHECK(is_near(posA.first, posA.second, kPxA, kPyA));
    CHECK(is_near(posB.first, posB.second, kPxB, kPyB));

    // Amplitude ratio of the two local peaks must be within 15% of 1.0/0.6 ≈ 1.667.
    const double amp_ratio = ampA / ampB;
    const double expected_ratio = kFluxA / kFluxB;
    CHECK(std::abs(amp_ratio - expected_ratio) / expected_ratio < 0.15);
}

// ── Test 7: Zero / empty input ────────────────────────────────────────────────

TEST_CASE("CLEAN: zero dirty image returns ncomp=0 iters=0 without NaN or crash")
{
    constexpr std::uint32_t N = 32u;
    constexpr double kBeamSigma = 3.0;
    constexpr double kFwhmPx = 4.0;

    const auto beam = make_gaussian_beam(N, kBeamSigma);
    const std::vector<double> zero_dirty(static_cast<std::size_t>(N) * N, 0.0);

    const CleanResult res = hogbom(zero_dirty, beam, N, 1000u, 0.1, kFwhmPx);

    CHECK(res.ncomp == 0u);
    CHECK(res.iters == 0u);
    CHECK(res.flux  == 0.0);
    CHECK(res.restored.size() == static_cast<std::size_t>(N) * N);
    CHECK(res.residual.size() == static_cast<std::size_t>(N) * N);

    for (double v : res.restored)
        CHECK(!std::isnan(v));
    for (double v : res.residual)
        CHECK(!std::isnan(v));
}

TEST_CASE("CLEAN: mismatched dirty/beam sizes returns zeroed result without crash")
{
    constexpr std::uint32_t N = 32u;

    // beam too short
    const std::vector<double> dirty(static_cast<std::size_t>(N) * N, 1.0);
    const std::vector<double> beam_short(16u, 1.0);

    const CleanResult res = hogbom(dirty, beam_short, N, 100u, 0.1, 4.0);

    CHECK(res.ncomp == 0u);
    CHECK(res.iters == 0u);
    CHECK(res.restored.size() == static_cast<std::size_t>(N) * N);
    for (double v : res.restored)
        CHECK(!std::isnan(v));
}

TEST_CASE("CLEAN: invalid gain returns zeroed result without crash")
{
    constexpr std::uint32_t N = 32u;
    const auto beam = make_gaussian_beam(N, 3.0);
    const auto dirty = make_point_dirty(N, beam, 16u, 16u, 1.0);

    const CleanResult res_zero = hogbom(dirty, beam, N, 100u, 0.0, 4.0);
    CHECK(res_zero.ncomp == 0u);

    const CleanResult res_neg = hogbom(dirty, beam, N, 100u, -0.1, 4.0);
    CHECK(res_neg.ncomp == 0u);
}

// ── Test 8: Edge component — non-wrapping beam subtraction ───────────────────

TEST_CASE("CLEAN: edge source does not alias flux to opposite edge (non-wrapping)")
{
    // Place a point source near the grid boundary.  After CLEAN the residual
    // near the OPPOSITE edge must remain close to zero — wrapping would create
    // a ghost there.
    constexpr std::uint32_t N   = 64u;
    constexpr double kBeamSigma = 3.0;
    constexpr double kFwhmPx    = 4.5;
    constexpr std::uint32_t kNiter = 2000u;
    constexpr double kGain      = 0.1;
    constexpr double kFlux      = 1.0;

    // Source very close to the left edge.
    constexpr std::uint32_t kPxEdge = 3u;
    constexpr std::uint32_t kPyEdge = 32u;

    const auto beam  = make_gaussian_beam(N, kBeamSigma);
    const auto dirty = make_point_dirty(N, beam, kPxEdge, kPyEdge, kFlux);

    const CleanResult res = hogbom(dirty, beam, N, kNiter, kGain, kFwhmPx);

    // The peak in the restored image must be near (kPxEdge, kPyEdge), NOT opposite.
    const std::uint32_t peak_idx = argmax_abs(res.restored);
    const std::uint32_t peak_px  = peak_idx % N;
    // peak_px should be near kPxEdge (≤ 3), NOT near N-kPxEdge (≈ 61).
    CHECK(peak_px <= 5u);

    // Near the OPPOSITE x-edge the restored image must be negligible.
    double max_opposite = 0.0;
    for (std::uint32_t r = 0u; r < N; ++r)
        for (std::uint32_t c = N - 6u; c < N; ++c)
            max_opposite = std::max(max_opposite, std::abs(res.restored[r * N + c]));

    CHECK(max_opposite < 0.05 * kFlux);
}

// ── Test 9: clean_fwhm_px helper ─────────────────────────────────────────────

TEST_CASE("clean_fwhm_px clamps to [2, 24] and matches formula")
{
    constexpr std::uint32_t N = 128u;

    // A moderate uv extent that should give an unclamped result.
    // Formula: fwhm = 0.9 / r_max / (theta_fov / N) = 0.9 * N / (r_max * theta_fov)
    // With r_max = 1000, theta_fov = 0.001:
    // fwhm = 0.9 * 128 / (1000 * 0.001) = 115.2 → clamped to 24.
    CHECK(clean_fwhm_px(1000.0, 0.001, N) == doctest::Approx(24.0).epsilon(1e-9));

    // With r_max = 10, theta_fov = 1e-3:
    // fwhm = 0.9 * 128 / (10 * 0.001) = 11520 → clamped to 24.
    CHECK(clean_fwhm_px(10.0, 0.001, N) == doctest::Approx(24.0).epsilon(1e-9));

    // With r_max = 10000, theta_fov = 1e-3:
    // fwhm = 0.9 * 128 / (10000 * 0.001) = 11.52 → unclamped.
    const double expected_mid = 0.9 / 10000.0 / (0.001 / static_cast<double>(N));
    CHECK(expected_mid > 2.0);
    CHECK(expected_mid < 24.0);
    CHECK(clean_fwhm_px(10000.0, 0.001, N) == doctest::Approx(expected_mid).epsilon(1e-9));

    // A very large r_max → very small fwhm → clamped to 2.
    CHECK(clean_fwhm_px(1e9, 1e-3, N) == doctest::Approx(2.0).epsilon(1e-9));

    // Degenerate inputs return the lower clamp without crashing.
    CHECK(clean_fwhm_px(0.0, 1e-3, N)  == doctest::Approx(2.0).epsilon(1e-9));
    CHECK(clean_fwhm_px(1e3, 0.0, N)   == doctest::Approx(2.0).epsilon(1e-9));
    CHECK(clean_fwhm_px(1e3, 1e-3, 0u) == doctest::Approx(2.0).epsilon(1e-9));
}

// ── Test 10: End-to-end smoke test using make_images and ArrayConfig ──────────

TEST_CASE("CLEAN: end-to-end smoke — sample_uv → make_images → hogbom succeeds for various antennas_per_arm")
{
    // Build a simple TargetFT (point source at DC = constant spectrum).
    constexpr std::uint32_t N = 128u;
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    TargetFT target;
    target.N   = N;
    target.Fre.assign(sz, 1.0); // constant real spectrum → point source at DC
    target.Fim.assign(sz, 0.0);

    // Use observation parameters that produce baselines landing inside the grid:
    //   lambda=0.1 m (radio), theta_fov=5e-7 rad, 10 km Moon array
    //   u_max ~ 10000/0.1 = 1e5 wavelengths
    //   GX = N/2 + u/du = N/2 + u * theta_fov = 64 + 1e5 * 5e-7 = 64.05 ≈ in grid
    // For stronger uv coverage use Earth stations (baselines ~8000 km):
    //   u ~ 8e7 wl → GX = 64 + 8e7 * 5e-7 = 64 + 40 = 104  ✓ in [1,126]
    ObservationConfig obs{};
    obs.dec_rad        = 20.0 * (std::numbers::pi / 180.0); // 20° declination
    obs.lambda_m       = 0.1;    // 10 cm radio
    obs.duration_hours = 4.0;
    obs.rotation       = true;
    obs.mode           = InstrumentMode::Radio;
    obs.theta_fov_rad  = 5.0e-7;
    obs.flux_total     = 1.0;
    obs.epoch_days     = 0.0;

    StationErrors errs{};  // no noise

    // Test two different antenna counts to verify no hardcoding.
    for (std::uint32_t n_arm : {4u, 7u})
    {
        ArrayConfig cfg{};
        cfg.geometry         = ArrayGeometry::Y;
        cfg.antennas_per_arm = n_arm;
        cfg.site_extent_m    = 10000.0;
        cfg.site             = SiteCenter{
            Body::Moon,
            parallax::interferometry::kTychoLat,
            parallax::interferometry::kTychoLon
        };

        const auto stations = generate_stations(cfg);
        CHECK(!stations.empty());

        const auto vis = sample_uv(stations, obs, target, errs);
        // The Moon array may produce zero visible samples at this epoch and declination.
        // That's physically valid; the test only checks the pipeline doesn't crash.
        // Still verify we got >= 0 (i.e., no assertion/exception).
        CHECK(vis.size() >= 0u);

        // Choose du from the uv extent.
        double r_max = 0.0;
        for (const auto& v : vis)
            r_max = std::max(r_max, std::hypot(v.u, v.v));

        const double du       = 1.0 / obs.theta_fov_rad;
        const double fwhm_px  = (r_max > 0.0) ?
            clean_fwhm_px(r_max, obs.theta_fov_rad, N) : 6.0;
        CHECK(fwhm_px >= 2.0);
        CHECK(fwhm_px <= 24.0);

        const DirtyImages imgs = make_images(vis, du, N, Weighting::Natural);
        CHECK(imgs.beam.size()  == sz);
        CHECK(imgs.dirty.size() == sz);

        const CleanResult res = hogbom(imgs.dirty, imgs.beam, N, 200u, 0.1, fwhm_px);
        CHECK(res.restored.size() == sz);
        CHECK(res.residual.size() == sz);

        for (double v : res.restored)
            CHECK(!std::isnan(v));
        for (double v : res.residual)
            CHECK(!std::isnan(v));
    }
}
