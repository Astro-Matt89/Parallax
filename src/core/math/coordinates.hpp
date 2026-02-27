#pragma once
// core/math/coordinates.hpp - Celestial coordinate conversions
// Supports equatorial (RA/Dec), horizontal (Az/Alt), and galactic systems.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace parallax {

// -----------------------------------------------------------------------
// Fundamental constants
// -----------------------------------------------------------------------
static constexpr double kPi      = std::numbers::pi;
static constexpr double kTwoPi   = 2.0 * kPi;
static constexpr double kDegRad  = kPi / 180.0;
static constexpr double kRadDeg  = 180.0 / kPi;
static constexpr double kArcsecRad = kDegRad / 3600.0;

// -----------------------------------------------------------------------
// Equatorial coordinates (J2000 epoch unless noted)
// -----------------------------------------------------------------------
struct Equatorial {
    double ra_deg{0.0};   // Right Ascension [degrees, 0-360]
    double dec_deg{0.0};  // Declination [degrees, -90..+90]
};

// -----------------------------------------------------------------------
// Horizontal (topocentric) coordinates
// -----------------------------------------------------------------------
struct Horizontal {
    double az_deg{0.0};   // Azimuth [degrees, N=0, E=90]
    double alt_deg{0.0};  // Altitude [degrees, 0..90]
};

// -----------------------------------------------------------------------
// Geographic location of the observer
// -----------------------------------------------------------------------
struct GeographicLocation {
    double lat_deg{0.0};  // Geodetic latitude [degrees, N positive]
    double lon_deg{0.0};  // Longitude [degrees, E positive]
    double elevation_m{0.0}; // Elevation above sea level [meters]
};

// -----------------------------------------------------------------------
// Coordinate conversion utilities
// -----------------------------------------------------------------------

/// Normalise an angle to [0, 360)
inline double normDeg(double deg) {
    deg = std::fmod(deg, 360.0);
    return (deg < 0.0) ? deg + 360.0 : deg;
}

/// Equatorial -> Horizontal for a given sidereal time and latitude.
/// @param eq       Equatorial coordinates [degrees]
/// @param ha_deg   Hour angle [degrees, positive westward]
/// @param lat_deg  Observer latitude [degrees]
inline Horizontal equatorialToHorizontal(const Equatorial& eq,
                                          double ha_deg,
                                          double lat_deg) {
    const double ha  = ha_deg  * kDegRad;
    const double dec = eq.dec_deg * kDegRad;
    const double lat = lat_deg * kDegRad;

    const double sinAlt = std::sin(dec)*std::sin(lat)
                        + std::cos(dec)*std::cos(lat)*std::cos(ha);
    const double alt = std::asin(std::clamp(sinAlt, -1.0, 1.0));

    const double cosAz = (std::sin(dec) - std::sin(alt)*std::sin(lat))
                       / (std::cos(alt)*std::cos(lat) + 1e-15);
    double az = std::acos(std::clamp(cosAz, -1.0, 1.0));
    if (std::sin(ha) > 0.0) az = kTwoPi - az;

    return {normDeg(az * kRadDeg), alt * kRadDeg};
}

/// Hour angle given Local Sidereal Time and Right Ascension (degrees).
inline double hourAngle(double lst_deg, double ra_deg) {
    return normDeg(lst_deg - ra_deg);
}

/// Airmass from altitude (Pickering 2002 approximation).
/// Returns airmass (>=1; undefined for alt<=0 — clamped to 40).
inline double airmass(double alt_deg) {
    if (alt_deg <= 0.0) return 40.0;
    const double r = 1.0 /
        std::sin((alt_deg + 244.0 / (165.0 + 47.0*std::pow(alt_deg, 1.1))) * kDegRad);
    return std::min(r, 40.0);
}

/// Angular separation between two equatorial positions [degrees].
inline double angularSeparation(const Equatorial& a, const Equatorial& b) {
    const double ra1  = a.ra_deg  * kDegRad;
    const double dec1 = a.dec_deg * kDegRad;
    const double ra2  = b.ra_deg  * kDegRad;
    const double dec2 = b.dec_deg * kDegRad;

    const double cosC = std::sin(dec1)*std::sin(dec2)
                      + std::cos(dec1)*std::cos(dec2)*std::cos(ra1 - ra2);
    return std::acos(std::clamp(cosC, -1.0, 1.0)) * kRadDeg;
}

} // namespace parallax
