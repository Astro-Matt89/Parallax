#pragma once
// universe/star.hpp - Core stellar data model

#include "core/math/coordinates.hpp"
#include <string>
#include <cstdint>

namespace parallax {

// -----------------------------------------------------------------------
// Spectral classification
// -----------------------------------------------------------------------
enum class SpectralClass : uint8_t {
    O, B, A, F, G, K, M,
    L, T, Y,     // brown dwarfs
    WR,          // Wolf-Rayet
    Unknown
};

/// Map spectral class -> approximate effective temperature [K]
inline double effectiveTemp(SpectralClass sc) {
    switch (sc) {
        case SpectralClass::O:  return 40000.0;
        case SpectralClass::B:  return 20000.0;
        case SpectralClass::A:  return  8500.0;
        case SpectralClass::F:  return  6500.0;
        case SpectralClass::G:  return  5500.0;
        case SpectralClass::K:  return  4000.0;
        case SpectralClass::M:  return  3000.0;
        case SpectralClass::L:  return  1700.0;
        case SpectralClass::T:  return   900.0;
        case SpectralClass::Y:  return   400.0;
        case SpectralClass::WR: return 50000.0;
        default:                return  5778.0; // solar default
    }
}

// -----------------------------------------------------------------------
// RGB colour approximation for a blackbody at temperature T
// -----------------------------------------------------------------------
struct Colour { float r{1.f}, g{1.f}, b{1.f}; };

/// Simple blackbody colour approximation (Tanner 2012 fit).
inline Colour blackbodyColour(double T) {
    // clamp to visible range
    if (T < 1000.0) T = 1000.0;
    if (T > 40000.0) T = 40000.0;

    double r, g, b;
    // Red channel
    if (T <= 6600.0) {
        r = 1.0;
    } else {
        r = 329.698727446 * std::pow((T / 100.0) - 60.0, -0.1332047592) / 255.0;
    }
    // Green channel
    if (T <= 6600.0) {
        g = (99.4708025861 * std::log(T / 100.0) - 161.1195681661) / 255.0;
    } else {
        g = 288.1221695283 * std::pow((T / 100.0) - 60.0, -0.0755148492) / 255.0;
    }
    // Blue channel
    if (T >= 6600.0) {
        b = 1.0;
    } else if (T <= 1900.0) {
        b = 0.0;
    } else {
        b = (138.5177312231 * std::log((T / 100.0) - 10.0) - 305.0447927307) / 255.0;
    }

    auto clamp01 = [](double v) { return (float)std::max(0.0, std::min(1.0, v)); };
    return {clamp01(r), clamp01(g), clamp01(b)};
}

// -----------------------------------------------------------------------
// Star record
// -----------------------------------------------------------------------
struct Star {
    uint64_t    id{0};              ///< Catalog identifier
    std::string name;               ///< Common/catalog name (may be empty)
    Equatorial  position;           ///< Equatorial coordinates J2000
    double      distance_pc{0.0};  ///< Distance in parsecs (0 = unknown)
    double      v_magnitude{0.0};  ///< Visual (V-band) apparent magnitude
    double      abs_magnitude{0.0};///< Absolute magnitude (if known)
    double      parallax_mas{0.0}; ///< Parallax [milli-arcsec]
    SpectralClass spectral_class{SpectralClass::Unknown};
    double      proper_motion_ra{0.0};  ///< μα* [mas/yr]
    double      proper_motion_dec{0.0}; ///< μδ  [mas/yr]
    bool        is_variable{false};
    bool        is_procedural{false};   ///< Generated, not from real catalog

    /// Colour derived from effective temperature
    Colour colour() const {
        return blackbodyColour(effectiveTemp(spectral_class));
    }

    /// Angular diameter [milli-arcsec] given radius in solar radii
    /// (used for rendering very bright stars)
    double angularDiameter_mas(double radius_solar) const {
        if (distance_pc <= 0.0) return 0.0;
        // 1 solar radius = 6.957e8 m; 1 pc = 3.086e16 m; result in mas
        return (2.0 * radius_solar * 6.957e8 / (distance_pc * 3.086e16))
               * (180.0 / kPi) * 3.6e6;
    }
};

} // namespace parallax
