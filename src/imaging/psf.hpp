#pragma once

/// @file psf.hpp
/// @brief Point Spread Function (PSF) model for the Glasswing Array.
///
/// Implements a Gaussian approximation of the Airy disk:
///
///   FWHM (radians) = 1.028 × λ / D
///
/// where λ is the observing wavelength and D is the aperture diameter.
/// The true Airy disk is I(θ) = [2 J₁(x)/x]² with x = π D sinθ / λ,
/// but the Gaussian approximation is sufficient for Sprint 10a imaging.
///
/// A point source with `total_flux` electrons is spread over the detector by
/// evaluating a normalised 2D Gaussian centred at the sub-pixel position, then
/// accumulating (`+=`) into an existing Image.  Only a bounded kernel window
/// of ±3σ is evaluated for performance.

#include "imaging/image.hpp"

#include "core/types.hpp"

namespace parallax::imaging
{
    /// @brief PSF utilities for the Glasswing Array (Gaussian approximation).
    class Psf
    {
    public:
        // -- FWHM helpers ---------------------------------------------------

        /// @brief FWHM of the diffraction-limited PSF in radians.
        ///
        /// Uses the Gaussian approximation of the Airy disk:
        ///   FWHM_rad = 1.028 × wavelength_nm / (aperture_diameter_m × 1e9)
        ///
        /// @param wavelength_nm      Observing wavelength (nm).
        /// @param aperture_diameter_m Largest active aperture diameter (m).
        [[nodiscard]] static f64 fwhm_rad(f64 wavelength_nm, f64 aperture_diameter_m);

        /// @brief FWHM of the PSF in detector pixels.
        ///
        /// Converts the angular FWHM to pixels using the detector pixel scale:
        ///   FWHM_px = FWHM_rad / pixel_scale_rad_per_px
        ///
        /// @param wavelength_nm           Observing wavelength (nm).
        /// @param aperture_diameter_m     Largest active aperture diameter (m).
        /// @param pixel_scale_arcsec_per_px Detector pixel scale (arcsec/px).
        [[nodiscard]] static f64 fwhm_px(f64 wavelength_nm,
                                          f64 aperture_diameter_m,
                                          f64 pixel_scale_arcsec_per_px);

        // -- Rendering ------------------------------------------------------

        /// @brief Render a point source into an image via a Gaussian PSF.
        ///
        /// Spreads `total_flux` electrons over the detector pixels centred at the
        /// sub-pixel position (x_sub, y_sub) using a 2D Gaussian with:
        ///   σ = psf_fwhm_px / 2.3548   (= FWHM / (2·√(2·ln2)))
        ///
        /// Only pixels within ±3σ of the centre are touched (kernel window is
        /// clamped to image bounds).  The kernel weights are normalised so that
        /// flux is conserved for sources fully inside the image; sources near
        /// the edge lose flux proportional to the clipped weight fraction.
        ///
        /// Values are accumulated (`+=`) into @p image; multiple sources may be
        /// rendered on the same image without overwriting earlier results.
        ///
        /// @param image        Target Image to accumulate into.
        /// @param x_sub        Sub-pixel column coordinate of the source centre.
        /// @param y_sub        Sub-pixel row coordinate of the source centre.
        /// @param total_flux   Total flux to deposit (electrons).
        /// @param psf_fwhm_px  PSF full-width at half-maximum (pixels).
        static void render_point_source(Image& image,
                                        f64    x_sub,
                                        f64    y_sub,
                                        f64    total_flux,
                                        f64    psf_fwhm_px);
    };

} // namespace parallax::imaging
