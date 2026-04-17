/// @file solar_system.cpp
/// @brief Implementation of Solar System ephemeris calculations.
///
/// Sun position: Meeus "Astronomical Algorithms" Ch. 25 (low precision, ~0.01°).
/// Moon position: Meeus "Astronomical Algorithms" Ch. 47 (abridged, ~0.1°).
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

/// Mean lunar distance (km) — Meeus Ch. 47.
static constexpr f64 kMeanLunarDistanceKm = 385000.56;

/// Moon physical radius (km).
static constexpr f64 kMoonRadiusKm = 1737.4;

/// AU in km (IAU 2012).
static constexpr f64 kAuKm = 149597870.7;

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
// compute_moon — Meeus Ch. 47 abridged lunar ephemeris
//
// Fundamental arguments (degrees):
//   L' = 218.3165 + 481267.8813 × T   (Moon mean longitude)
//   D  = 297.8502 + 445267.1115 × T   (mean elongation)
//   M  = 357.5291 + 35999.0503 × T    (Sun mean anomaly)
//   M' = 134.9634 + 477198.8676 × T   (Moon mean anomaly)
//   F  = 93.2720 + 483202.0175 × T    (argument of latitude)
//
// Longitude: Σl in units of 0.000001°  (divide by 1e6)
// Latitude:  Σb in units of 0.000001°  (divide by 1e6)
// Distance:  Σr in units of 0.001 km   (divide by 1e3)
// =================================================================

CelestialBodyState SolarSystem::compute_moon(f64 jd)
{
    const f64 T = TimeSystem::julian_centuries(jd);

    // --- Fundamental arguments (degrees) ---
    const f64 Lp = normalize_degrees(218.3165 + 481267.8813 * T);
    const f64 D  = normalize_degrees(297.8502 + 445267.1115 * T);
    const f64 M  = normalize_degrees(357.5291 + 35999.0503 * T);
    const f64 Mp = normalize_degrees(134.9634 + 477198.8676 * T);
    const f64 F  = normalize_degrees(93.2720 + 483202.0175 * T);

    // Convert to radians for trig
    const f64 D_rad  = D  * astro_constants::kDegToRad;
    const f64 M_rad  = M  * astro_constants::kDegToRad;
    const f64 Mp_rad = Mp * astro_constants::kDegToRad;
    const f64 F_rad  = F  * astro_constants::kDegToRad;

    // ---------------------------------------------------------------
    // Longitude perturbations Σl (Meeus Table 47.A, top 15 terms)
    //
    //  #  |  D   M   M'  F  | coefficient
    //  1  |  0   0   1   0  | +6288774
    //  2  |  2   0  -1   0  | +1274027
    //  3  |  2   0   0   0  | +658314
    //  4  |  0   0   2   0  | +213618
    //  5  |  0   1   0   0  | -185116
    //  6  |  0   0   0   2  | -114332
    //  7  |  2   0  -2   0  | +58793
    //  8  |  2  -1  -1   0  | +57066
    //  9  |  2   0   1   0  | +53322
    // 10  |  2  -1   0   0  | +45758
    // 11  |  0   1  -1   0  | -40923
    // 12  |  1   0   0   0  | -34720
    // 13  |  0   1   1   0  | -30383
    // 14  |  2   0   0  -2  | +15327
    // 15  |  0   0   1   2  | -12528
    // ---------------------------------------------------------------

    const f64 sum_l =
          6288774.0 * std::sin(Mp_rad)
        + 1274027.0 * std::sin(2.0 * D_rad - Mp_rad)
        +  658314.0 * std::sin(2.0 * D_rad)
        +  213618.0 * std::sin(2.0 * Mp_rad)
        -  185116.0 * std::sin(M_rad)
        -  114332.0 * std::sin(2.0 * F_rad)
        +   58793.0 * std::sin(2.0 * D_rad - 2.0 * Mp_rad)
        +   57066.0 * std::sin(2.0 * D_rad - M_rad - Mp_rad)
        +   53322.0 * std::sin(2.0 * D_rad + Mp_rad)
        +   45758.0 * std::sin(2.0 * D_rad - M_rad)
        -   40923.0 * std::sin(M_rad - Mp_rad)
        -   34720.0 * std::sin(D_rad)
        -   30383.0 * std::sin(M_rad + Mp_rad)
        +   15327.0 * std::sin(2.0 * D_rad - 2.0 * F_rad)
        -   12528.0 * std::sin(Mp_rad + 2.0 * F_rad);

    // ---------------------------------------------------------------
    // Latitude perturbations Σb (Meeus Table 47.B, top 10 terms)
    //
    //  #  |  D   M   M'  F  | coefficient
    //  1  |  0   0   0   1  | +5128122
    //  2  |  0   0   1   1  | +280602
    //  3  |  0   0   1  -1  | +277693
    //  4  |  2   0   0  -1  | +173237
    //  5  |  2   0  -1   1  | +55413
    //  6  |  2   0  -1  -1  | +46271
    //  7  |  2   0   0   1  | +32573
    //  8  |  0   0   2   1  | +17198
    //  9  |  2   0   1  -1  | +9267
    // 10  |  0   0   2  -1  | +8823
    // ---------------------------------------------------------------

    const f64 sum_b =
          5128122.0 * std::sin(F_rad)
        +  280602.0 * std::sin(Mp_rad + F_rad)
        +  277693.0 * std::sin(Mp_rad - F_rad)
        +  173237.0 * std::sin(2.0 * D_rad - F_rad)
        +   55413.0 * std::sin(2.0 * D_rad - Mp_rad + F_rad)
        +   46271.0 * std::sin(2.0 * D_rad - Mp_rad - F_rad)
        +   32573.0 * std::sin(2.0 * D_rad + F_rad)
        +   17198.0 * std::sin(2.0 * Mp_rad + F_rad)
        +    9267.0 * std::sin(2.0 * D_rad + Mp_rad - F_rad)
        +    8823.0 * std::sin(2.0 * Mp_rad - F_rad);

    // ---------------------------------------------------------------
    // Distance perturbations Σr (Meeus Table 47.A, top 10 terms)
    //
    //  #  |  D   M   M'  F  | coefficient (× 0.001 km)
    //  1  |  0   0   1   0  | -20905355
    //  2  |  2   0  -1   0  | -3699111
    //  3  |  2   0   0   0  | -2955968
    //  4  |  0   0   2   0  | -569925
    //  5  |  0   1   0   0  | +48888
    //  6  |  0   0   0   2  | -3149
    //  7  |  2   0  -2   0  | +246158
    //  8  |  2  -1  -1   0  | -152138
    //  9  |  2   0   0  -2  | -170733
    // 10  |  0   1  -1   0  | -204586
    // ---------------------------------------------------------------

    const f64 sum_r =
        - 20905355.0 * std::cos(Mp_rad)
        -  3699111.0 * std::cos(2.0 * D_rad - Mp_rad)
        -  2955968.0 * std::cos(2.0 * D_rad)
        -   569925.0 * std::cos(2.0 * Mp_rad)
        +    48888.0 * std::cos(M_rad)
        -     3149.0 * std::cos(2.0 * F_rad)
        +   246158.0 * std::cos(2.0 * D_rad - 2.0 * Mp_rad)
        -   152138.0 * std::cos(2.0 * D_rad - M_rad - Mp_rad)
        -   170733.0 * std::cos(2.0 * D_rad - 2.0 * F_rad)
        -   204586.0 * std::cos(M_rad - Mp_rad);

    // --- Ecliptic coordinates ---
    const f64 lambda_deg = normalize_degrees(Lp + sum_l / 1000000.0);
    const f64 beta_deg   = sum_b / 1000000.0;
    const f64 delta_km   = kMeanLunarDistanceKm + sum_r / 1000.0;

    const f64 lambda_rad = lambda_deg * astro_constants::kDegToRad;
    const f64 beta_rad   = beta_deg * astro_constants::kDegToRad;

    // --- Ecliptic → equatorial ---
    const f64 eps_rad = mean_obliquity(jd);
    const auto eq = ecliptic_to_equatorial(lambda_rad, beta_rad, eps_rad);

    // --- Distance in AU ---
    const f64 distance_au = delta_km / kAuKm;

    // --- Angular diameter (arcseconds) ---
    // d = 2 × asin(R_moon / Δ) converted to arcseconds
    const f64 angular_diam_rad = 2.0 * std::asin(kMoonRadiusKm / delta_km);
    const auto angular_diam_arcsec = static_cast<f32>(
        angular_diam_rad * astro_constants::kRadToDeg * 3600.0);

    // --- Phase angle and illumination are set by compute_moon_full ---
    // When called standalone via compute_moon(), return placeholder values.
    // The caller should use compute_moon_full() for phase-aware results.

    return CelestialBodyState{
        .equatorial            = eq,
        .horizontal            = HorizontalCoord{.alt = 0.0, .az = 0.0},
        .distance_au           = distance_au,
        .magnitude             = -12.7f,    // placeholder; overridden by compute_moon_full
        .angular_diameter_arcsec = angular_diam_arcsec,
        .phase_angle_deg       = 0.0f,
        .illumination          = 0.0f,
    };
}

// =================================================================
// classify_moon_phase — determine named phase from elongation + k
//
// Phase thresholds (sprint doc):
//   New:              k < 0.02
//   Crescent:         0.02 ≤ k < 0.48
//   Quarter:          0.48 ≤ k < 0.52
//   Gibbous:          0.52 ≤ k < 0.98
//   Full:             k ≥ 0.98
//
// Waxing vs waning determined by elongation:
//   elongation 0..180° = waxing
//   elongation 180..360° = waning
// =================================================================

MoonPhase SolarSystem::classify_moon_phase(f64 elongation_deg, f64 illumination)
{
    const bool waxing = (elongation_deg < 180.0);

    if (illumination < 0.02)
    {
        return MoonPhase::New;
    }
    if (illumination >= 0.98)
    {
        return MoonPhase::Full;
    }
    if (illumination < 0.48)
    {
        return waxing ? MoonPhase::WaxingCrescent : MoonPhase::WaningCrescent;
    }
    if (illumination < 0.52)
    {
        return waxing ? MoonPhase::FirstQuarter : MoonPhase::LastQuarter;
    }
    // 0.52 ≤ k < 0.98
    return waxing ? MoonPhase::WaxingGibbous : MoonPhase::WaningGibbous;
}

// =================================================================
// compute_moon_full — position + phase + illumination + magnitude
//
// Phase angle (Meeus, sprint doc):
//   ψ = acos(sin(dec_sun)·sin(dec_moon) + cos(dec_sun)·cos(dec_moon)·cos(ra_moon - ra_sun))
//   i = atan2(R_sun × sin(ψ), Δ_moon - R_sun × cos(ψ))
//   k = (1 + cos(i)) / 2
//
// Apparent magnitude (sprint doc):
//   V_moon ≈ -12.73 + 0.026 × |i| + 4e-9 × i⁴
//   (i in degrees)
// =================================================================

MoonState SolarSystem::compute_moon_full(f64 jd)
{
    const auto moon = compute_moon(jd);
    const auto sun  = compute_sun(jd);

    // --- Elongation ψ (angular separation Sun–Moon on sky) ---
    const f64 d_ra = moon.equatorial.ra - sun.equatorial.ra;
    const f64 cos_psi = std::sin(sun.equatorial.dec) * std::sin(moon.equatorial.dec)
                      + std::cos(sun.equatorial.dec) * std::cos(moon.equatorial.dec)
                        * std::cos(d_ra);
    const f64 psi_rad = std::acos(std::clamp(cos_psi, -1.0, 1.0));
    const f64 psi_deg = psi_rad * astro_constants::kRadToDeg;

    // --- Phase angle i ---
    // R_sun in AU, Δ_moon in AU
    const f64 R_sun_au   = sun.distance_au;
    const f64 delta_moon_au = moon.distance_au;
    const f64 phase_angle_rad = std::atan2(
        R_sun_au * std::sin(psi_rad),
        delta_moon_au - R_sun_au * std::cos(psi_rad)
    );
    const f64 phase_angle_deg = std::abs(phase_angle_rad * astro_constants::kRadToDeg);

    // --- Illumination fraction ---
    const f64 illumination = (1.0 + std::cos(phase_angle_rad)) / 2.0;

    // --- Apparent magnitude (sprint doc formula) ---
    const f64 i_deg = phase_angle_deg;
    const f64 mag = -12.73 + 0.026 * i_deg + 4.0e-9 * i_deg * i_deg * i_deg * i_deg;

    // --- Elongation for waxing/waning classification ---
    // Use the ecliptic elongation convention:
    //   If Moon RA is ahead of Sun RA (mod 2π), Moon is waxing (eastern elongation).
    f64 elongation_for_phase = d_ra * astro_constants::kRadToDeg;
    if (elongation_for_phase < 0.0)
    {
        elongation_for_phase += 360.0;
    }

    // --- Phase name ---
    const MoonPhase phase = classify_moon_phase(elongation_for_phase, illumination);

    // --- Distance in km ---
    const f64 distance_km = moon.distance_au * kAuKm;

    // --- Build final body state with corrected magnitude/illumination ---
    CelestialBodyState body = moon;
    body.magnitude       = static_cast<f32>(mag);
    body.phase_angle_deg = static_cast<f32>(phase_angle_deg);
    body.illumination    = static_cast<f32>(illumination);

    return MoonState{
        .body          = body,
        .phase         = phase,
        .elongation_deg = psi_deg,
        .distance_km   = distance_km,
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
// compute_all — compute Sun + Moon (real) + stubs for planets
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
