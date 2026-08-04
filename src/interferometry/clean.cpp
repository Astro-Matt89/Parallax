#include "interferometry/clean.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <vector>

namespace parallax::interferometry
{

// ── Constants ─────────────────────────────────────────────────────────────────

/// 2 × sqrt(2 × ln2) — converts Gaussian FWHM to sigma.
static constexpr double kFwhmToSigma = 2.3548200450309493;

/// Stopping threshold: break when peak < this fraction of the initial peak.
static constexpr double kStopFraction = 0.02;

/// Truncation radius for the restore Gaussian, in units of sigma.
/// ±4σ captures > 99.994% of a Gaussian's integral — negligible flux loss.
/// Documented choice: wider truncation would slow large component counts without
/// measurably changing the restored image.
static constexpr double kGaussianTruncSigma = 4.0;

// ── Helpers ───────────────────────────────────────────────────────────────────

/// Find the flat index of the element with the largest absolute value in `v`.
/// Returns {absolute_peak_value, flat_index}.
static std::pair<double, std::uint32_t> find_peak(const std::vector<double>& v)
{
    double best_abs = 0.0;
    std::uint32_t best_idx = 0u;
    for (std::uint32_t i = 0u; i < static_cast<std::uint32_t>(v.size()); ++i)
    {
        const double a = std::abs(v[i]);
        if (a > best_abs)
        {
            best_abs = a;
            best_idx = i;
        }
    }
    return {best_abs, best_idx};
}

/// Subtract `f × beam_shifted` from `res`.
///
/// The dirty beam is centred at (N/2, N/2).  To subtract at component pixel
/// (cx, cy) the beam shift offsets are:  dx = cx − N/2, dy = cy − N/2.
///
/// Boundary handling: NON-WRAPPING.  Pixels that would land outside [0, N) are
/// skipped.  This prevents a component near one grid edge from aliasing flux to
/// the opposite edge — a real imaging artefact that circular-wrap would introduce.
static void subtract_beam(std::vector<double>& res,
                           const std::vector<double>& beam,
                           std::uint32_t N,
                           std::uint32_t cx,
                           std::uint32_t cy,
                           double f)
{
    const std::int32_t dx = static_cast<std::int32_t>(cx) - static_cast<std::int32_t>(N / 2u);
    const std::int32_t dy = static_cast<std::int32_t>(cy) - static_cast<std::int32_t>(N / 2u);
    const std::int32_t iN = static_cast<std::int32_t>(N);

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
            res[static_cast<std::uint32_t>(ry) * N + static_cast<std::uint32_t>(rx)] -=
                f * beam[static_cast<std::uint32_t>(by) * N + static_cast<std::uint32_t>(bx)];
        }
    }
}

/// Add a unit-peak circular Gaussian centred at (cx, cy) scaled by flux `f`
/// to `image`.
///
/// The Gaussian is evaluated over a truncated support of ±kGaussianTruncSigma σ
/// for efficiency; flux outside this radius is negligible (< 3.2 × 10⁻⁸ of peak).
///
/// Normalisation: the Gaussian has peak amplitude 1.0 (unit-peak, NOT unit-integral).
/// A component of flux f therefore contributes f to the restored image peak.
/// This matches the sandbox's `f × Gaussian` convention and preserves absolute
/// amplitude in the restored image.
static void add_gaussian(std::vector<double>& image,
                          std::uint32_t N,
                          std::uint32_t cx,
                          std::uint32_t cy,
                          double f,
                          double sigma)
{
    const double inv2s2 = 1.0 / (2.0 * sigma * sigma);
    const std::int32_t radius = static_cast<std::int32_t>(std::ceil(kGaussianTruncSigma * sigma));
    const std::int32_t iN = static_cast<std::int32_t>(N);
    const std::int32_t icx = static_cast<std::int32_t>(cx);
    const std::int32_t icy = static_cast<std::int32_t>(cy);

    for (std::int32_t dy = -radius; dy <= radius; ++dy)
    {
        const std::int32_t ry = icy + dy;
        if (ry < 0 || ry >= iN)
            continue;
        for (std::int32_t dx = -radius; dx <= radius; ++dx)
        {
            const std::int32_t rx = icx + dx;
            if (rx < 0 || rx >= iN)
                continue;
            const double r2 = static_cast<double>(dx * dx + dy * dy);
            image[static_cast<std::uint32_t>(ry) * N + static_cast<std::uint32_t>(rx)] +=
                f * std::exp(-r2 * inv2s2);
        }
    }
}

// ── hogbom ────────────────────────────────────────────────────────────────────

CleanResult hogbom(const std::vector<double>& dirty,
                   const std::vector<double>& beam,
                   std::uint32_t N,
                   std::uint32_t niter,
                   double gain,
                   double fwhm_px)
{
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    // Input validation — return zeroed result on error (no throw).
    if (dirty.size() != sz || beam.size() != sz)
    {
        spdlog::error("hogbom: dirty.size()={} beam.size()={} but N*N={}; "
                      "returning empty result.", dirty.size(), beam.size(), sz);
        CleanResult empty{};
        empty.restored.assign(sz, 0.0);
        empty.residual.assign(sz, 0.0);
        empty.ncomp = 0u;
        empty.iters = 0u;
        empty.flux  = 0.0;
        return empty;
    }

    if (gain <= 0.0 || gain > 1.0)
    {
        spdlog::error("hogbom: gain={} is outside (0,1]; returning empty result.", gain);
        CleanResult empty{};
        empty.restored.assign(sz, 0.0);
        empty.residual.assign(sz, 0.0);
        empty.ncomp = 0u;
        empty.iters = 0u;
        empty.flux  = 0.0;
        return empty;
    }

    // Initialise residual from dirty image.
    std::vector<double> res(dirty);

    // Compute initial absolute peak — stopping threshold is relative to this.
    const auto [pk0, _] = find_peak(res);

    if (pk0 == 0.0)
    {
        spdlog::warn("hogbom: dirty image is all-zero; returning empty result.");
        CleanResult empty{};
        empty.restored.assign(sz, 0.0);
        empty.residual.assign(sz, 0.0);
        empty.ncomp = 0u;
        empty.iters = 0u;
        empty.flux  = 0.0;
        return empty;
    }

    const double threshold = kStopFraction * pk0;

    // CLEAN loop.
    std::vector<CleanComponent> components;
    std::uint32_t iters = 0u;

    for (std::uint32_t it = 0u; it < niter; ++it)
    {
        const auto [pk, pi] = find_peak(res);

        if (pk < threshold)
            break;

        // Signed flux: negative features are legitimate dirty-image artefacts.
        const double f = gain * res[pi];

        const std::uint32_t px = pi % N;
        const std::uint32_t py = pi / N;

        components.push_back(CleanComponent{px, py, f});

        // Subtract gain × beam shifted to (px, py) — non-wrapping (see header).
        subtract_beam(res, beam, N, px, py, f);

        iters = it + 1u;
    }

    // Restore: residual + Σ component Gaussians.
    // Gaussian sigma from FWHM: sigma = fwhm_px / (2√(2 ln 2)).
    const double sigma = fwhm_px / kFwhmToSigma;

    std::vector<double> restored(res); // start from residual
    for (const auto& comp : components)
        add_gaussian(restored, N, comp.px, comp.py, comp.flux, sigma);

    // Accumulate statistics.
    double total_flux = 0.0;
    for (const auto& comp : components)
        total_flux += comp.flux;

    // Save size before move (aggregate init evaluates left-to-right;
    // moving components first would leave size() == 0 for the ncomp field).
    const auto ncomp = static_cast<std::uint32_t>(components.size());

    return CleanResult{
        std::move(restored),
        std::move(res),
        std::move(components),
        ncomp,
        iters,
        total_flux
    };
}

// ── clean_fwhm_px ─────────────────────────────────────────────────────────────

double clean_fwhm_px(double r_max_uv, double theta_fov_rad, std::uint32_t N)
{
    static constexpr double kFwhmMin = 2.0;
    static constexpr double kFwhmMax = 24.0;

    if (r_max_uv <= 0.0 || theta_fov_rad <= 0.0 || N == 0u)
        return kFwhmMin;

    // Pixel size in uv-space: du = theta_fov_rad / N  (radians → wavelengths/pixel).
    const double pixel_size = theta_fov_rad / static_cast<double>(N);
    const double fwhm = 0.9 / r_max_uv / pixel_size;

    return std::clamp(fwhm, kFwhmMin, kFwhmMax);
}

} // namespace parallax::interferometry
