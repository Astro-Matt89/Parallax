/// @file solar_system.cpp
/// @brief Implementation of Solar System ephemeris calculations.
///
/// Sun position: Meeus "Astronomical Algorithms" Ch. 25 (low precision, ~0.01°).
/// Moon position: Meeus "Astronomical Algorithms" Ch. 47 (abridged, ~0.1°).
/// Planet positions: Meeus "Astronomical Algorithms" Ch. 31 (Keplerian elements, ~0.1–0.5°).
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
// Meeus Table 31.A — Earth's orbital elements (J2000.0 equinox)
//
// Referred to the mean ecliptic and equinox of J2000.0.
// Each element is a polynomial in T (Julian centuries from J2000.0):
//   value = c[0] + c[1]·T + c[2]·T² + c[3]·T³
//
// Meeus "Astronomical Algorithms" 2nd ed., Table 31.A, row for Earth.
// Do NOT include Earth in the planet table — kEarthElements is separate.
// =================================================================

/// @brief Earth's Keplerian orbital elements (Meeus Table 31.A).
///
/// Used exclusively by compute_earth_heliocentric().
/// Earth is not a valid planet_id target; these elements are kept separate.
static constexpr OrbitalElements kEarthElements = {
    .L     = {100.466449,   35999.3728519,  -0.00000568,   -0.000000026},
    .a     = {1.000001018,  0.0},
    .e     = {0.01670862,  -0.000042037,    -0.0000001236,   0.00000000004},
    .i     = {0.0,          0.0130546,      -0.00000931,    -0.000000034},
    .omega = {174.873174,  -0.2410908,       0.00004067,    -0.000001327},
    .pi    = {102.937348,   1.7195366,       0.00045688,    -0.000000018},
};

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
// get_planet_elements — Meeus Table 31.A, 7 planets (Earth excluded)
//
// Returns a pointer into a static constexpr table of OrbitalElements
// indexed by planet_id (1=Mercury, 2=Venus, 4=Mars, 5=Jupiter,
// 6=Saturn, 7=Uranus, 8=Neptune).  Earth (id=3) is not in this
// table — use kEarthElements instead.
//
// Returns nullptr for any planet_id that is not one of the seven.
// =================================================================

const OrbitalElements* SolarSystem::get_planet_elements(u32 planet_id)
{
    // Meeus "Astronomical Algorithms" 2nd ed., Table 31.A.
    // Referred to the mean ecliptic and equinox of J2000.0.
    // Layout: [0]=Mercury [1]=Venus [2]=Mars [3]=Jupiter [4]=Saturn [5]=Uranus [6]=Neptune
    static constexpr OrbitalElements kPlanetTable[7] =
    {
        // ── Mercury (planet_id = 1) ───────────────────────────────────────────
        {
            .L     = {252.250906,   149474.0722491,   0.00030350,    0.000000018},
            .a     = {0.387098310,  0.0},
            .e     = {0.20563175,   0.000020406,     -0.0000000284, -0.00000000017},
            .i     = {7.004986,     0.0018215,        -0.00001809,    0.000000053},
            .omega = {48.330893,    1.1861883,          0.00017542,    0.000000215},
            .pi    = {77.456119,    1.5564775,          0.00029589,    0.000000056},
        },
        // ── Venus (planet_id = 2) ─────────────────────────────────────────────
        {
            .L     = {181.979801,   58519.2130302,    0.00031014,    0.000000015},
            .a     = {0.723329820,  0.0},
            .e     = {0.00677188,  -0.000047766,      0.0000000975,  0.00000000044},
            .i     = {3.394662,     0.0010037,        -0.00000088,   -0.000000007},
            .omega = {76.679920,    0.9011190,          0.00040665,   -0.000000080},
            .pi    = {131.563703,   1.4022288,         -0.00107618,   -0.000005765},
        },
        // ── Mars (planet_id = 4) ──────────────────────────────────────────────
        {
            .L     = {355.433275,   19141.6964746,    0.00031097,    0.000000015},
            .a     = {1.523679342,  0.0},
            .e     = {0.09340062,   0.000090483,     -0.0000000806, -0.00000000035},
            .i     = {1.849726,    -0.0006010,         0.00001276,   -0.000000007},
            .omega = {49.558093,    0.7720959,          0.00001557,    0.000002267},
            .pi    = {336.060234,   1.8410449,          0.00013477,    0.000000536},
        },
        // ── Jupiter (planet_id = 5) ───────────────────────────────────────────
        {
            .L     = {34.351519,    3036.3027748,     0.00022330,    0.000000037},
            .a     = {5.202603191,  0.0000001913},
            .e     = {0.04849485,   0.000163244,     -0.0000004719, -0.00000000197},
            .i     = {1.303270,    -0.0054966,         0.00000465,   -0.000000004},
            .omega = {100.464441,   1.0209550,          0.00040117,    0.000000569},
            .pi    = {14.331309,    1.6126668,          0.00103127,   -0.000004569},
        },
        // ── Saturn (planet_id = 6) ────────────────────────────────────────────
        {
            .L     = {50.077444,    1223.5110686,     0.00051908,   -0.000000030},
            .a     = {9.554909596, -0.0000021389},
            .e     = {0.05550862,  -0.000346818,     -0.0000006456,  0.00000000338},
            .i     = {2.488878,    -0.0037363,        -0.00001516,    0.000000089},
            .omega = {113.665524,   0.8770979,         -0.00012067,   -0.000002380},
            .pi    = {93.057237,    1.9637613,          0.00083753,    0.000004928},
        },
        // ── Uranus (planet_id = 7) ────────────────────────────────────────────
        {
            .L     = {314.055005,   429.8640561,      0.00030434,    0.000000026},
            .a     = {19.218446062, -0.0000000372},
            .e     = {0.04629590,  -0.000027337,      0.0000000790,  0.00000000025},
            .i     = {0.773196,     0.0007744,          0.00003749,   -0.000000092},
            .omega = {74.005957,    0.5211278,          0.00133947,    0.000018484},
            .pi    = {173.005291,   1.4863790,          0.00021406,    0.000000434},
        },
        // ── Neptune (planet_id = 8) ───────────────────────────────────────────
        {
            .L     = {304.348665,   219.8833092,      0.00030882,    0.000000018},
            .a     = {30.110386869, -0.0000001663},
            .e     = {0.00898809,   0.000006408,     -0.0000000008, -0.00000000005},
            .i     = {1.769953,    -0.0093082,        -0.00000708,    0.000000027},
            .omega = {131.784057,   1.1022057,          0.00026006,   -0.000000636},
            .pi    = {48.120276,    1.4262957,          0.00038434,    0.000000020},
        },
    };

    // Map planet_id to table index.
    // Note: the parameter name 'planet_id' shadows the planet_id namespace here,
    // so we use raw integer literals for case labels.
    switch (planet_id)
    {
        case 1: return &kPlanetTable[0];  // Mercury
        case 2: return &kPlanetTable[1];  // Venus
        case 4: return &kPlanetTable[2];  // Mars
        case 5: return &kPlanetTable[3];  // Jupiter
        case 6: return &kPlanetTable[4];  // Saturn
        case 7: return &kPlanetTable[5];  // Uranus
        case 8: return &kPlanetTable[6];  // Neptune
        default: return nullptr;          // Earth (3) or any invalid id
    }
}

// =================================================================
// solve_kepler — Newton–Raphson solution of Kepler's equation
//
// Kepler's equation:  M = E − e·sin(E)
//
// Given mean anomaly M and eccentricity e, find eccentric anomaly E.
//
// Algorithm (Meeus Ch. 30):
//   Initial guess: E₀ = M + e·sin(M)
//   Iterate: Eₙ₊₁ = Eₙ − (Eₙ − e·sin(Eₙ) − M) / (1 − e·cos(Eₙ))
//   Terminate when |ΔE| < 1e-10 or after 20 iterations.
//
// Convergence is guaranteed for all e < 1 (elliptical orbits).
// For e = 0 (circular), ΔE = 0 on the first iteration.
// =================================================================

f64 SolarSystem::solve_kepler(f64 M_rad, f64 e)
{
    static constexpr int kMaxIterations = 20;
    static constexpr f64 kTolerance     = 1.0e-10;

    // Initial guess — good first approximation for small e
    f64 E = M_rad + e * std::sin(M_rad);

    for (int iter = 0; iter < kMaxIterations; ++iter)
    {
        const f64 dE = (E - e * std::sin(E) - M_rad) / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::abs(dE) < kTolerance)
        {
            break;
        }
    }

    return E;
}

// =================================================================
// compute_heliocentric — heliocentric ecliptic Cartesian from elements
//
// Algorithm (Meeus Ch. 31):
//   1. Evaluate each orbital element as a polynomial in T.
//   2. Normalize angular elements to [0°, 360°).
//   3. Derive M (mean anomaly) = L − ϖ.
//   4. Derive ω (argument of perihelion) = ϖ − Ω.
//   5. Solve Kepler's equation for eccentric anomaly E.
//   6. Compute true anomaly ν and heliocentric distance r.
//   7. Rotate into ecliptic Cartesian (Meeus Eq. 31.4):
//      x = r·(cos(Ω)·cos(ω+ν) − sin(Ω)·sin(ω+ν)·cos(i))
//      y = r·(sin(Ω)·cos(ω+ν) + cos(Ω)·sin(ω+ν)·cos(i))
//      z = r·(sin(ω+ν)·sin(i))
// =================================================================

HeliocentricPos SolarSystem::compute_heliocentric(const OrbitalElements& el, f64 T)
{
    // --- Step 1: evaluate polynomials in T ---
    const f64 L_deg = normalize_degrees(
        el.L[0] + el.L[1] * T + el.L[2] * T * T + el.L[3] * T * T * T);

    const f64 a = el.a[0] + el.a[1] * T;

    const f64 e = el.e[0] + el.e[1] * T + el.e[2] * T * T + el.e[3] * T * T * T;

    const f64 i_deg = el.i[0] + el.i[1] * T + el.i[2] * T * T + el.i[3] * T * T * T;

    const f64 omega_deg = normalize_degrees(
        el.omega[0] + el.omega[1] * T + el.omega[2] * T * T + el.omega[3] * T * T * T);

    const f64 pi_deg = normalize_degrees(
        el.pi[0] + el.pi[1] * T + el.pi[2] * T * T + el.pi[3] * T * T * T);

    // --- Step 2: derived angular quantities ---
    const f64 M_deg = normalize_degrees(L_deg - pi_deg);          // mean anomaly
    const f64 w_deg = normalize_degrees(pi_deg - omega_deg);      // argument of perihelion

    const f64 M_rad     = M_deg     * astro_constants::kDegToRad;
    const f64 i_rad     = i_deg     * astro_constants::kDegToRad;
    const f64 omega_rad = omega_deg * astro_constants::kDegToRad;
    const f64 w_rad     = w_deg     * astro_constants::kDegToRad;

    // --- Step 3: eccentric anomaly via Newton–Raphson ---
    const f64 E = solve_kepler(M_rad, e);

    // --- Step 4: true anomaly ---
    // ν = 2·atan2(√(1+e)·sin(E/2), √(1−e)·cos(E/2))
    const f64 nu = 2.0 * std::atan2(
        std::sqrt(1.0 + e) * std::sin(E / 2.0),
        std::sqrt(1.0 - e) * std::cos(E / 2.0)
    );

    // --- Step 5: heliocentric distance ---
    const f64 r = a * (1.0 - e * std::cos(E));

    // --- Step 6: ecliptic Cartesian (Meeus Eq. 31.4) ---
    // u = argument of latitude = ω + ν
    const f64 u = w_rad + nu;

    const f64 cos_omega = std::cos(omega_rad);
    const f64 sin_omega = std::sin(omega_rad);
    const f64 cos_u     = std::cos(u);
    const f64 sin_u     = std::sin(u);
    const f64 cos_i     = std::cos(i_rad);
    const f64 sin_i     = std::sin(i_rad);

    return HeliocentricPos{
        .x = r * (cos_omega * cos_u - sin_omega * sin_u * cos_i),
        .y = r * (sin_omega * cos_u + cos_omega * sin_u * cos_i),
        .z = r * (sin_u * sin_i),
    };
}

// =================================================================
// compute_earth_heliocentric — Earth's heliocentric ecliptic position
//
// Uses kEarthElements (Meeus Table 31.A, Earth row) and the same
// Keplerian algorithm as compute_heliocentric.  Earth's inclination
// is approximately 0° so z ≈ 0, but non-zero T terms are included.
// =================================================================

HeliocentricPos SolarSystem::compute_earth_heliocentric(f64 T)
{
    return compute_heliocentric(kEarthElements, T);
}

// =================================================================
// planet_angular_diameter_at_1au — equatorial angular diameter (arcsec)
//
// Published values for each planet's equatorial disk at 1 AU distance.
// Source: Meeus "Astronomical Algorithms" 2nd ed. / IAU standard data.
//
// Apparent diameter at actual distance Δ AU = diameter_at_1au / Δ.
// =================================================================

f64 SolarSystem::planet_angular_diameter_at_1au(u32 planet_id)
{
    // Note: parameter 'planet_id' shadows the planet_id namespace;
    // use raw literals for the case labels.
    switch (planet_id)
    {
        case 1: return 6.74;    // Mercury
        case 2: return 16.92;   // Venus
        case 4: return 9.36;    // Mars
        case 5: return 196.74;  // Jupiter
        case 6: return 165.6;   // Saturn (disk only, no rings)
        case 7: return 70.0;    // Uranus
        case 8: return 67.9;    // Neptune
        default: return 0.0;
    }
}

// =================================================================
// compute_planet_magnitude — apparent visual magnitude
//
// Simplified phase-dependent formulas from Meeus Ch. 41.
//
//   V = H + 5·log10(r·Δ) + phase_correction(i)
//
// where i = phase angle in degrees, r = heliocentric dist (AU),
// Δ = geocentric dist (AU).
//
// Saturn: ring-tilt term omitted (out of scope for Task 6.3).
// Venus:  valid for i < 163.7°; the formula is used without clamping
//         since conjunction geometry rarely produces larger angles.
// =================================================================

f32 SolarSystem::compute_planet_magnitude(
    u32 planet_id, f64 r, f64 delta, f64 phase_angle_deg)
{
    const f64 i        = phase_angle_deg;
    const f64 log10_rD = std::log10(r * delta);
    f64 V = 0.0;

    // Note: parameter 'planet_id' shadows the planet_id namespace.
    switch (planet_id)
    {
        case 1:  // Mercury
            V = -0.42 + 5.0 * log10_rD
                + 0.0380 * i
                - 0.000273 * i * i
                + 2.0e-6  * i * i * i;
            break;

        case 2:  // Venus
            V = -4.40 + 5.0 * log10_rD
                + 0.0009 * i
                + 2.39e-7 * i * i * i;
            break;

        case 4:  // Mars
            V = -1.52 + 5.0 * log10_rD + 0.016 * i;
            break;

        case 5:  // Jupiter
            V = -9.40 + 5.0 * log10_rD + 0.005 * i;
            break;

        case 6:  // Saturn — ring-tilt correction omitted (Task 6.3 scope)
            V = -8.88 + 5.0 * log10_rD;
            break;

        case 7:  // Uranus
            V = -7.19 + 5.0 * log10_rD + 0.002 * i;
            break;

        case 8:  // Neptune
            V = -6.87 + 5.0 * log10_rD;
            break;

        default:
            V = 0.0;
            break;
    }

    return static_cast<f32>(V);
}

// =================================================================
// compute_planet_state — geocentric state from heliocentric positions
//
// Given the heliocentric ecliptic positions of a planet and Earth,
// computes all fields of CelestialBodyState except horizontal (Alt/Az),
// which is observer-dependent and set to zero by convention.
//
// Steps:
//   1. Geocentric ecliptic Cartesian = planet − earth.
//   2. Geocentric distance Δ.
//   3. Ecliptic λ/β → equatorial RA/Dec.
//   4. Phase angle via the cosine rule on the Sun–Planet–Earth triangle.
//   5. Illumination fraction k = (1 + cos(phase)) / 2.
//   6. Apparent magnitude from compute_planet_magnitude().
//   7. Angular diameter = diameter_at_1au / Δ.
// =================================================================

CelestialBodyState SolarSystem::compute_planet_state(
    const HeliocentricPos& planet,
    const HeliocentricPos& earth,
    f64 r_helio,
    u32 planet_id,
    f64 eps_rad)
{
    // --- Step 1: geocentric ecliptic Cartesian ---
    const f64 X = planet.x - earth.x;
    const f64 Y = planet.y - earth.y;
    const f64 Z = planet.z - earth.z;

    // --- Step 2: geocentric distance Δ (AU) ---
    const f64 delta = std::sqrt(X * X + Y * Y + Z * Z);

    // --- Step 3: ecliptic longitude λ and latitude β ---
    const f64 lambda = std::atan2(Y, X);
    const f64 beta   = std::asin(std::clamp(Z / delta, -1.0, 1.0));

    // Ecliptic → equatorial
    const EquatorialCoord eq = ecliptic_to_equatorial(lambda, beta, eps_rad);

    // --- Step 4: phase angle ---
    // Earth's heliocentric distance R
    const f64 R = std::sqrt(earth.x * earth.x + earth.y * earth.y + earth.z * earth.z);

    // Cosine rule on the Sun–Planet–Earth triangle:
    //   cos(phase) = (r² + Δ² − R²) / (2·r·Δ)
    const f64 cos_phase = std::clamp(
        (r_helio * r_helio + delta * delta - R * R) / (2.0 * r_helio * delta),
        -1.0, 1.0
    );
    const f64 phase_rad = std::acos(cos_phase);
    const f64 phase_deg = phase_rad * astro_constants::kRadToDeg;

    // --- Step 5: illumination fraction ---
    const f64 illumination = (1.0 + std::cos(phase_rad)) / 2.0;

    // --- Step 6: apparent magnitude ---
    const f32 magnitude = compute_planet_magnitude(planet_id, r_helio, delta, phase_deg);

    // --- Step 7: angular diameter (arcsec) ---
    const f32 angular_diam = static_cast<f32>(planet_angular_diameter_at_1au(planet_id) / delta);

    return CelestialBodyState{
        .equatorial              = eq,
        .horizontal              = HorizontalCoord{.alt = 0.0, .az = 0.0},  // caller computes
        .distance_au             = delta,
        .magnitude               = magnitude,
        .angular_diameter_arcsec = angular_diam,
        .phase_angle_deg         = static_cast<f32>(phase_deg),
        .illumination            = static_cast<f32>(illumination),
    };
}

// =================================================================
// compute_planet — Meeus Ch. 31 Keplerian planet ephemeris
//
// Replaces the stub from Task 6.3.  Full pipeline:
//   1. Look up Meeus Table 31.A orbital elements for planet_id.
//   2. Evaluate elements as polynomials in T (Julian centuries).
//   3. Solve Kepler's equation for eccentric anomaly E.
//   4. Compute heliocentric ecliptic Cartesian for planet and Earth.
//   5. Convert to geocentric equatorial (RA/Dec).
//   6. Derive phase angle, illumination, magnitude, angular diameter.
//
// Horizontal coordinates (Alt/Az) are left zero — the caller (SkyState
// or the renderer) computes them using the observer location and LST.
//
// Returns a default-constructed (zeroed) state for invalid planet_id
// values (e.g. Earth=3 is not a valid target).
// =================================================================

CelestialBodyState SolarSystem::compute_planet(f64 jd, u32 planet_id)
{
    const OrbitalElements* elements = get_planet_elements(planet_id);
    if (!elements)
    {
        return CelestialBodyState{};  // invalid id — caller should not use this
    }

    const f64 T = TimeSystem::julian_centuries(jd);

    const HeliocentricPos planet_pos = compute_heliocentric(*elements, T);
    const HeliocentricPos earth_pos  = compute_earth_heliocentric(T);

    const f64 r_helio = std::sqrt(
        planet_pos.x * planet_pos.x +
        planet_pos.y * planet_pos.y +
        planet_pos.z * planet_pos.z
    );

    const f64 eps_rad = mean_obliquity(jd);

    return compute_planet_state(planet_pos, earth_pos, r_helio, planet_id, eps_rad);
}

// =================================================================
// compute_all — compute Sun + Moon + all 7 planets
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
