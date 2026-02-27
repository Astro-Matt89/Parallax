// rendering/star_field.cpp
#include "star_field.hpp"
#include "core/math/coordinates.hpp"
#include <cmath>

namespace parallax {

// -----------------------------------------------------------------------
// Gnomonic projection
// -----------------------------------------------------------------------
bool StarField::projectToScreen(const Equatorial& star_pos,
                                 const Equatorial& centre,
                                 double fov_w_deg, double fov_h_deg,
                                 int /*img_w*/, int /*img_h*/,
                                 double& out_x, double& out_y) {
    // Convert to radians
    double ra0  = centre.ra_deg   * kDegRad;
    double dec0 = centre.dec_deg  * kDegRad;
    double ra   = star_pos.ra_deg  * kDegRad;
    double dec  = star_pos.dec_deg * kDegRad;

    // Gnomonic projection
    double cos_c = std::sin(dec0)*std::sin(dec)
                 + std::cos(dec0)*std::cos(dec)*std::cos(ra - ra0);
    if (cos_c <= 1e-10) return false; // behind the projection plane

    double x = std::cos(dec)*std::sin(ra - ra0) / cos_c;
    double y = (std::cos(dec0)*std::sin(dec)
                - std::sin(dec0)*std::cos(dec)*std::cos(ra - ra0)) / cos_c;

    // Projected coordinates in degrees
    double x_deg = x * kRadDeg;
    double y_deg = y * kRadDeg;

    // Check within FOV
    if (std::abs(x_deg) > fov_w_deg * 0.5 ||
        std::abs(y_deg) > fov_h_deg * 0.5) return false;

    // Map to normalised [0,1] coordinates (renderer maps to viewport)
    out_x = ( x_deg / (fov_w_deg * 0.5) + 1.0) * 0.5;
    out_y = (-y_deg / (fov_h_deg * 0.5) + 1.0) * 0.5;
    return true;
}

// -----------------------------------------------------------------------
// StarField::build
// -----------------------------------------------------------------------
std::vector<StarRenderRecord> StarField::build(
    const std::vector<const Star*>& stars,
    const Telescope& scope,
    const AtmosphericModel& atm,
    const Equatorial& fov_centre,
    double fov_w_deg, double fov_h_deg,
    int image_w, int image_h,
    double alt_deg)
{
    std::vector<StarRenderRecord> records;
    records.reserve(stars.size());

    double seeing = atm.effectiveSeeing_arcsec(alt_deg);
    double psf_px = psfFwhm_pixels(scope, seeing);

    // The dimmest star we might render drives the brightness scale.
    // Use V=6 (naked-eye limit) as 1% intensity reference.
    constexpr double kRefMag = 0.0; // Sirius-class = 100% intensity

    bool is_reflector = (scope.central_obstruction > 0.0);

    for (const Star* s : stars) {
        // Atmospheric extinction
        double app_mag = atm.apparentMagnitude(s->v_magnitude, alt_deg);

        double px, py;
        if (!projectToScreen(s->position, fov_centre,
                              fov_w_deg, fov_h_deg,
                              image_w, image_h, px, py)) continue;

        StarRenderRecord rec;
        rec.screen_x   = px;
        rec.screen_y   = py;
        rec.magnitude  = app_mag;
        rec.brightness = magnitudeToIntensity(app_mag, kRefMag);
        rec.psf_radius_px = psf_px;
        rec.colour     = s->colour();
        rec.has_diffraction_spikes = is_reflector && (app_mag < 3.0);
        records.push_back(rec);
    }

    return records;
}

} // namespace parallax
