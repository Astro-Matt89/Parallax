#pragma once
// rendering/star_field.hpp - Starfield rendering data model
//
// Computes rendered star sizes, brightnesses, and PSF parameters
// for a given telescope + atmosphere combination.  The actual pixel
// output is backend-agnostic (the renderer consumes StarRenderRecord).

#include "universe/star.hpp"
#include "observatory/telescope.hpp"
#include "observatory/atmosphere.hpp"
#include <vector>
#include <cmath>

namespace parallax {

// -----------------------------------------------------------------------
// StarRenderRecord - per-star rendering parameters
// -----------------------------------------------------------------------
struct StarRenderRecord {
    double screen_x{0.0};   ///< Pixel column (fractional)
    double screen_y{0.0};   ///< Pixel row (fractional)
    double brightness{0.0}; ///< Normalised intensity [0..1]
    double psf_radius_px{1.0}; ///< PSF FWHM radius in pixels
    Colour colour;           ///< Blackbody-derived colour
    bool   has_diffraction_spikes{false}; ///< Reflector star with spikes
    double magnitude{0.0};  ///< Apparent magnitude for reference
};

// -----------------------------------------------------------------------
// StarField
// -----------------------------------------------------------------------
class StarField {
public:
    /// Build a rendered star field for the given list of stars.
    ///
    /// @param stars         Stars visible in the FOV (already filtered)
    /// @param scope         Telescope being used
    /// @param atm           Atmospheric model
    /// @param fov_centre    Field centre (equatorial)
    /// @param fov_w_deg     Field width [degrees]
    /// @param fov_h_deg     Field height [degrees]
    /// @param image_w       Output image width [pixels]
    /// @param image_h       Output image height [pixels]
    /// @param alt_deg       Altitude of field centre (for atmosphere)
    static std::vector<StarRenderRecord> build(
        const std::vector<const Star*>& stars,
        const Telescope& scope,
        const AtmosphericModel& atm,
        const Equatorial& fov_centre,
        double fov_w_deg, double fov_h_deg,
        int image_w, int image_h,
        double alt_deg = 45.0);

    /// Convert apparent magnitude to normalised pixel intensity.
    /// Follows a logarithmic (photometric) scale, clamped to [0,1].
    /// @param v_mag          Apparent magnitude of the star
    /// @param reference_mag  Magnitude that maps to intensity = 1.0
    static double magnitudeToIntensity(double v_mag, double reference_mag = 0.0) {
        double delta = reference_mag - v_mag;
        double intensity = std::pow(10.0, 0.4 * delta);
        return std::min(intensity, 1.0);
    }

    /// PSF FWHM in pixels from seeing and diffraction limit.
    static double psfFwhm_pixels(const Telescope& scope,
                                  double seeing_arcsec) {
        double dl = scope.diffractionLimit_arcsec();
        double fwhm_arcsec = std::hypot(seeing_arcsec, dl);
        return fwhm_arcsec / scope.pixelScale();
    }

private:
    /// Simple tangent-plane (gnomonic) projection of sky position
    /// to image pixel coordinates.
    static bool projectToScreen(const Equatorial& star_pos,
                                 const Equatorial& centre,
                                 double fov_w_deg, double fov_h_deg,
                                 int img_w, int img_h,
                                 double& out_x, double& out_y);
};

} // namespace parallax
