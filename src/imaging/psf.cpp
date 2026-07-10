/// @file psf.cpp
/// @brief PSF implementation — Gaussian approximation of the Airy disk.
///
/// Physics reference (docs/sprints/sprint_10a.md, Physics Reference):
///
///   True Airy disk:  I(θ) = [2 J₁(x) / x]²,  x = π D sinθ / λ
///
///   Gaussian approximation (used here):
///     FWHM_rad = 1.028 × λ / D
///     σ = FWHM / (2 · √(2 · ln2))   [= FWHM / 2.3548]
///
///   PSF width in pixels:
///     FWHM_px = FWHM_rad / pixel_scale_rad_per_px

#include "imaging/psf.hpp"

#include "core/types.hpp"

#include <cmath>

namespace parallax::imaging
{
    namespace
    {
        /// Conversion factor: FWHM → σ  (= 2 · √(2 · ln2)).
        constexpr f64 kFwhmToSigma = 2.3548200450309493;

        /// Half-kernel radius in units of σ.  ±3σ captures 99.7 % of the flux.
        constexpr f64 kKernelRadiusSigma = 3.0;

        /// Arcseconds per radian.
        constexpr f64 kArcSecPerRad = 180.0 * 3600.0 / 3.14159265358979323846;

        /// Nanometres per metre.
        constexpr f64 kNmPerMetre = 1.0e9;
    }

    // -------------------------------------------------------------------------
    // FWHM helpers
    // -------------------------------------------------------------------------

    f64 Psf::fwhm_rad(f64 wavelength_nm, f64 aperture_diameter_m)
    {
        // FWHM_rad = 1.028 × λ / D
        const f64 lambda_m = wavelength_nm / kNmPerMetre;
        return 1.028 * lambda_m / aperture_diameter_m;
    }

    f64 Psf::fwhm_px(f64 wavelength_nm,
                      f64 aperture_diameter_m,
                      f64 pixel_scale_arcsec_per_px)
    {
        const f64 fwhm_radians    = fwhm_rad(wavelength_nm, aperture_diameter_m);
        const f64 fwhm_arcsec     = fwhm_radians * kArcSecPerRad;
        return fwhm_arcsec / pixel_scale_arcsec_per_px;
    }

    // -------------------------------------------------------------------------
    // Point source rendering
    // -------------------------------------------------------------------------

    void Psf::render_point_source(Image& image,
                                   f64    x_sub,
                                   f64    y_sub,
                                   f64    total_flux,
                                   f64    psf_fwhm_px)
    {
        if (total_flux <= 0.0 || psf_fwhm_px <= 0.0)
        {
            return;
        }

        const f64 sigma     = psf_fwhm_px / kFwhmToSigma;
        const f64 sigma2    = sigma * sigma;
        const f64 inv_2sig2 = 1.0 / (2.0 * sigma2);

        // Bounded kernel window [x0, x1) × [y0, y1) clamped to image bounds.
        const i32 radius_px = static_cast<i32>(std::ceil(kKernelRadiusSigma * sigma));

        const i32 cx = static_cast<i32>(std::floor(x_sub));
        const i32 cy = static_cast<i32>(std::floor(y_sub));

        const i32 x0 = std::max(0, cx - radius_px);
        const i32 y0 = std::max(0, cy - radius_px);
        const i32 x1 = std::min(static_cast<i32>(image.width()),  cx + radius_px + 1);
        const i32 y1 = std::min(static_cast<i32>(image.height()), cy + radius_px + 1);

        if (x0 >= x1 || y0 >= y1)
        {
            return;  // Source is entirely outside the image.
        }

        // First pass: accumulate kernel weights so we can normalise.
        f64 weight_sum = 0.0;

        for (i32 py = y0; py < y1; ++py)
        {
            const f64 dy = static_cast<f64>(py) + 0.5 - y_sub;
            const f64 dy2 = dy * dy;

            for (i32 px = x0; px < x1; ++px)
            {
                const f64 dx = static_cast<f64>(px) + 0.5 - x_sub;
                weight_sum += std::exp(-(dx * dx + dy2) * inv_2sig2);
            }
        }

        if (weight_sum <= 0.0)
        {
            return;
        }

        // Second pass: deposit normalised flux into the image.
        const f64 flux_per_unit = total_flux / weight_sum;

        for (i32 py = y0; py < y1; ++py)
        {
            const f64 dy  = static_cast<f64>(py) + 0.5 - y_sub;
            const f64 dy2 = dy * dy;

            for (i32 px = x0; px < x1; ++px)
            {
                const f64 dx = static_cast<f64>(px) + 0.5 - x_sub;
                const f64 w  = std::exp(-(dx * dx + dy2) * inv_2sig2);

                image(static_cast<u32>(px), static_cast<u32>(py)) +=
                    static_cast<f32>(flux_per_unit * w);
            }
        }
    }

} // namespace parallax::imaging
