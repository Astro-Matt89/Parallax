/// @file solar_system.cpp
/// @brief Implementation of Solar System ephemeris calculations.
///
/// Sun position: Meeus "Astronomical Algorithms" Ch. 25 (low precision, ~0.01°).
/// All coefficients are the published Meeus values — not AI-generated.

#include "astro/solar_system.hpp"

#include "astro/time_system.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace parallax::astro
{

// =================================================================
// Constants
// =================================================================

/// Sun apparent visual magnitude (constant for skychart purposes).
static constexpr f32 kSunMagnitude = -26.74f;

/// Solar angular diameter at 1 AU, in arcseconds (Meeus).
static constexpr f64 kSunAngularDiameterAt1AU = 1919.26;

// =================================================================
// normalize_degrees — keep angle in [0, 360)
// =================================================================
f64 SolarSystem::normalize_degrees(f64 angle)
{
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0)
    {
        angle += 360.0;
    }
    return angle;
}

// =================================================================
// compute_sun_geometric — Meeus Ch. 25 intermediate values
//
// T = Julian centuries from J2000.0
//
// Geometric mean longitude:
//   L0 = 280.46646 + 36000.76983 × T + 0.0003032 × T²
//
// Mean anomaly:
//   M = 357.52911 + 35999.05029 × T - 0.0001537 × T²
//
// Eccentricity:
//   e = 0.016708634 - 0.000042037 × T - 0.0000001267 × T²
// =================================================================

void SolarSystem::compute_sun_geometric(f64 jd, f64& L0, f64& M, f64& e)
{
    const f64 T = TimeSystem::julian_centuries(jd);

    L0 = normalize_degrees(280.46646 + 36000.76983 * T + 0.0003032 * T * T);

    M = normalize_degrees(357.52911 + 35999.05029 * T - 0.0001537 * T * T);

    e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;
}

// =================================================================
// mean_obliquity — mean obliquity of the ecliptic
//
// ε₀ = 23.439291 - 0.013004×T - 0.000000164×T² + 0.000000504×T³
//
// Returns radians.
// =================================================================
f64 SolarSystem::mean_obliquity(f64 jd)
{
    const f64 T = TimeSystem::julian_centuries(jd);

    const f64 eps0_deg = 23.439291
                       - 0.013004 * T
                       - 0.000000164 * T * T
                       + 0.000000504 * T * T * T;

    return eps0_deg * astro_constants::kDegToRad;
}

// =================================================================
// ecliptic_to_equatorial — general ecliptic → equatorial conversion
//
// RA  = atan2(cos(ε) × sin(λ) - tan(β) × sin(ε), cos(λ))
// Dec = asin(sin(β) × cos(ε) + cos(β) × sin(ε) × sin(λ))
//
// When β = 0 (Sun on ecliptic), this simplifies to:
//   RA  = atan2(cos(ε) × sin(λ), cos(λ))
//   Dec = asin(sin(ε) × sin(λ))
// =================================================================

EquatorialCoord SolarSystem::ecliptic_to_equatorial(
    f64 lambda_rad, f64 beta_rad, f64 epsilon_rad)
{
    const f64 sin_lambda = std::sin(lambda_rad);
    const f64 cos_lambda = std::cos(lambda_rad);
    const f64 sin_beta   = std::sin(beta_rad);
    const f64 cos_beta   = std::cos(beta_rad);
    const f64 sin_eps    = std::sin(epsilon_rad);
    const f64 cos_eps    = std::cos(epsilon_rad);

    // RA = atan2(cos(ε)·sin(λ)·cos(β) − sin(β)·sin(ε),  cos(λ)·cos(β))
    const f64 ra = std::atan2(
        cos_eps * sin_lambda * cos_beta - sin_beta * sin_eps,
        cos_lambda * cos_beta
    );

    // Dec = asin(sin(β)·cos(ε) + cos(β)·sin(ε)·sin(λ))
    const f64 sin_dec = sin_beta * cos_eps + cos_beta * sin_eps * sin_lambda;
    const f64 dec = std::asin(std::clamp(sin_dec, -1.0, 1.0));

    // Normalize RA to [0, 2π)
    f64 ra_norm = std::fmod(ra, astro_constants::kTwoPi);
    if (ra_norm < 0.0)
    {
        ra_norm += astro_constants::kTwoPi;
    }

    return EquatorialCoord{
        .ra  = ra_norm,
        .dec = dec,
    };
}

// =================================================================
// compute_sun — Meeus Ch. 25 low-precision solar coordinates
// =================================================================

CelestialBodyState SolarSystem::compute_sun(f64 jd)
{
    const f64 T = TimeSystem::julian_centuries(jd);

    // --- Step 1: geometric quantities ---
    f64 L0_deg = 0.0;
    f64 M_deg  = 0.0;
    f64 e      = 0.0;
    compute_sun_geometric(jd, L0_deg, M_deg, e);

    const f64 M_rad = M_deg * astro_constants::kDegToRad;

    // --- Step 2: equation of center (Meeus Ch. 25) ---
    const f64 C_deg = (1.914602 - 0.004817 * T - 0.000014 * T * T) * std::sin(M_rad)
                    + (0.019993 - 0.000101 * T) * std::sin(2.0 * M_rad)
                    + 0.000289 * std::sin(3.0 * M_rad);

    // --- Step 3: true longitude ---
    const f64 lambda_true_deg = normalize_degrees(L0_deg + C_deg);

    // --- Step 4: true anomaly ---
    const f64 nu_deg = normalize_degrees(M_deg + C_deg);
    const f64 nu_rad = nu_deg * astro_constants::kDegToRad;

    // --- Step 5: distance (AU) ---
    const f64 R = 1.000001018 * (1.0 - e * e) / (1.0 + e * std::cos(nu_rad));

    // --- Step 6: apparent longitude ---
    // Ω = longitude of ascending node of Moon's mean orbit
    const f64 omega_deg = normalize_degrees(125.04 - 1934.136 * T);
    const f64 omega_rad = omega_deg * astro_constants::kDegToRad;

    // Apparent longitude: corrected for nutation and aberration
    const f64 lambda_apparent_deg = lambda_true_deg
                                  - 0.00569
                                  - 0.00478 * std::sin(omega_rad);

    const f64 lambda_apparent_rad = lambda_apparent_deg * astro_constants::kDegToRad;

    // --- Step 7: corrected obliquity ---
    const f64 eps0_rad = mean_obliquity(jd);
    const f64 eps_corrected_rad = eps0_rad
                                + 0.00256 * astro_constants::kDegToRad * std::cos(omega_rad);

    // --- Step 8: ecliptic → equatorial ---
    // Sun ecliptic latitude β = 0 (by definition for geocentric Sun)
    const auto eq = ecliptic_to_equatorial(lambda_apparent_rad, 0.0, eps_corrected_rad);

    // --- Step 9: physical properties ---
    const auto angular_diam = static_cast<f32>(kSunAngularDiameterAt1AU / R);

    return CelestialBodyState{
        .equatorial            = eq,
        .horizontal            = HorizontalCoord{.alt = 0.0, .az = 0.0},  // computed by caller
        .distance_au           = R,
        .magnitude             = kSunMagnitude,
        .angular_diameter_arcsec = angular_diam,
        .phase_angle_deg       = 0.0f,   // Sun has no phase angle from Earth
        .illumination          = 1.0f,    // Sun is always fully "illuminated"
    };
}

// =================================================================
// compute_moon — stub for Task 6.2
// =================================================================

CelestialBodyState SolarSystem::compute_moon([[maybe_unused]] f64 jd)
{
    return CelestialBodyState{
        .equatorial            = EquatorialCoord{.ra = 0.0, .dec = 0.0},
        .horizontal            = HorizontalCoord{.alt = 0.0, .az = 0.0},
        .distance_au           = 0.00257,   // ~average Moon distance in AU
        .magnitude             = -12.7f,
        .angular_diameter_arcsec = 1873.7f,  // ~average
        .phase_angle_deg       = 0.0f,
        .illumination          = 0.0f,
    };
}

// =================================================================
// compute_planet — stub for Task 6.3
// =================================================================

CelestialBodyState SolarSystem::compute_planet(
    [[maybe_unused]] f64 jd, [[maybe_unused]] u32 planet_id)
{
    return CelestialBodyState{
        .equatorial            = EquatorialCoord{.ra = 0.0, .dec = 0.0},
        .horizontal            = HorizontalCoord{.alt = 0.0, .az = 0.0},
        .distance_au           = 1.0,
        .magnitude             = 0.0f,
        .angular_diameter_arcsec = 0.0f,
        .phase_angle_deg       = 0.0f,
        .illumination          = 0.0f,
    };
}

// =================================================================
// compute_all — compute Sun (real) + stubs for Moon/planets
// =================================================================

SolarSystem::AllBodies SolarSystem::compute_all(f64 jd)
{
    AllBodies bodies{};

    bodies.sun  = compute_sun(jd);
    bodies.moon = compute_moon(jd);

    // Planet IDs: 1=Mercury, 2=Venus, 4=Mars, 5=Jupiter, 6=Saturn, 7=Uranus, 8=Neptune
    static constexpr std::array<u32, 7> kPlanetIds = {1, 2, 4, 5, 6, 7, 8};
    for (u32 i = 0; i < 7; ++i)
    {
        bodies.planets[i] = compute_planet(jd, kPlanetIds[i]);
    }

    return bodies;
}

} // namespace parallax::astro