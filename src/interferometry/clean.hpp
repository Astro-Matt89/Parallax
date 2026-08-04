#pragma once

/// @file clean.hpp
/// @brief Högbom CLEAN deconvolution for the Glasswing Array (Sprint 10b Task 10b.4).
///
/// ## Algorithm summary
///
/// The CLEAN loop operates on a residual image initialised to the dirty image:
///
///   1. pk0  = max|res|     (initial absolute peak — computed once)
///   2. loop up to niter times:
///        pi = argmax |res|
///        pk = |res[pi]|
///        if pk < 0.02 × pk0: break
///        f  = gain × res[pi]        // **signed**: negative features produce negative components
///        record component (px = pi%N, py = pi/N, flux = f)
///        subtract f × beam_shifted_to(px,py) from res
///   3. restore:
///        restored = res  +  Σ_k  f_k × Gaussian(sigma, px_k, py_k)
///        where sigma = fwhm_px / (2 × sqrt(2 × ln2))
///
/// ## Boundary choice (non-wrapping beam subtraction)
///
/// The dirty beam is centred at (N/2, N/2) after fftshift.  To subtract at
/// component position (px, py) the beam is shifted by (px − N/2, py − N/2).
/// Pixels that would fall outside the [0, N) range are **skipped** (clamped/
/// skipped, NOT wrapped).  This prevents a component near one edge from aliasing
/// flux to the opposite edge — a real artefact that wrapping would introduce.
///
/// ## Restore Gaussian normalisation
///
/// The Gaussian used to restore components is **unit-peak** (Gaussian(0) = 1),
/// matching the sandbox expression `f × Gaussian`.  A component of flux f
/// therefore contributes peak amplitude f to the restored image.  This is NOT
/// the same as a unit-integral Gaussian (which would scale flux by 1/area); the
/// unit-peak convention preserves absolute amplitude in the restored image.
///
/// ## fwhm_px helper
///
/// `clean_fwhm_px` derives the restore-beam FWHM from the uv extent, per the
/// sandbox formula.  Callers should use it so that 10b.8 (UI) shares one definition.

#include <cstdint>
#include <vector>

namespace parallax::interferometry
{
    // ── CLEAN component ───────────────────────────────────────────────────────

    /// A single CLEAN model component (delta function in the sky model).
    struct CleanComponent
    {
        std::uint32_t px;  ///< Grid x-coordinate (column index, = flat_index % N).
        std::uint32_t py;  ///< Grid y-coordinate (row index,    = flat_index / N).
        double flux;       ///< Accumulated flux for this component (may be negative).
    };

    // ── CLEAN result ──────────────────────────────────────────────────────────

    /// Output of `hogbom()`.
    struct CleanResult
    {
        std::vector<double> restored;         ///< N×N restored image, row-major.
        std::vector<double> residual;         ///< N×N residual image after CLEAN, row-major.
        std::vector<CleanComponent> components; ///< All CLEAN model components found.
        std::uint32_t ncomp;   ///< Number of components (== components.size()).
        std::uint32_t iters;   ///< Number of major-loop iterations executed.
        double flux;           ///< Total cleaned flux (sum of component fluxes).
    };

    // ── Högbom CLEAN ──────────────────────────────────────────────────────────

    /// Run the Högbom CLEAN deconvolution algorithm.
    ///
    /// @param dirty    N×N dirty image (row-major, fftshifted, beam-peak normalised).
    /// @param beam     N×N dirty beam  (row-major, fftshifted, beam-peak normalised).
    /// @param N        Grid side length in pixels.
    /// @param niter    Maximum number of major-loop iterations.
    /// @param gain     Loop gain ∈ (0, 1].  Typical value: 0.1.
    /// @param fwhm_px  FWHM of the restore (clean) beam in pixels.
    ///                 Use `clean_fwhm_px()` to derive this from the uv extent.
    /// @return         `CleanResult` with restored image, residual, components, and statistics.
    ///
    /// @note  If dirty.size() != N*N or beam.size() != N*N, or gain ∉ (0,1],
    ///        an error is logged and a zeroed CleanResult is returned (no throw).
    [[nodiscard]] CleanResult hogbom(const std::vector<double>& dirty,
                                     const std::vector<double>& beam,
                                     std::uint32_t N,
                                     std::uint32_t niter,
                                     double gain,
                                     double fwhm_px);

    // ── Restore-beam FWHM helper ──────────────────────────────────────────────

    /// Derive the CLEAN restore-beam FWHM from the uv-coverage extent.
    ///
    /// Formula (from sandbox):
    ///   fwhm_px = clamp( 0.9 / r_max_uv / (theta_fov_rad / N),  2.0, 24.0 )
    ///
    /// where `r_max_uv` is the maximum uv radius max(hypot(u,v)) over the
    /// visibility set and `theta_fov_rad / N` is the uv-cell size in wavelengths.
    ///
    /// @param r_max_uv      Maximum |uv| in wavelengths.
    /// @param theta_fov_rad Field-of-view half-angle (radians).
    /// @param N             Grid side length (pixels).
    [[nodiscard]] double clean_fwhm_px(double r_max_uv,
                                        double theta_fov_rad,
                                        std::uint32_t N);

} // namespace parallax::interferometry
