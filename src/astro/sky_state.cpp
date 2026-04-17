/// @file sky_state.cpp
/// @brief Sky condition calculator implementation.
///
/// Implements SkyConditionCalculator::compute and helpers.
///
/// Formula source: docs/sprints/sprint_06.md §6.4 (sprint doc is authoritative).
///
/// Moon sky-brightness delta model (sprint doc §6.4, simplified version):
///   delta = 2.5 * moon_illumination * sin(moon_alt_rad)
///   Full moon at zenith → 2.5 mag drop; quarter at 30° → ~0.625 mag.
///   Clamped to [0, 3.5] for safety.
///
/// Twilight limiting-magnitude penalties (sprint doc §6.4, problem statement fallback):
///   Day:              penalty = 10.0 mag
///   CivilTwilight:    penalty = 6.0 - (0.0 - sun_alt_deg) * (4.0 / 6.0)  → smooth 6→2 over [-6,0°]
///   NauticalTwilight: penalty = 4.0 - (-6.0 - sun_alt_deg) * (3.0 / 6.0) → smooth 4→1 over [-12,-6°]
///   AstroTwilight:    penalty = 1.0 - (-12.0 - sun_alt_deg) * (1.0 / 6.0)→ smooth 1→0 over [-18,-12°]
///   Night:            penalty = 0.0
///
/// Sprint doc deviation note:
///   The sprint doc §6.4 specifies bortle_scale as a parameter to compute().
///   This implementation instead accepts const Atmosphere& (as specified in the
///   problem statement) so Bortle-based logic is fully delegated to Atmosphere —
///   no duplication. The numerical output is identical; only the call site differs.

#include "astro/sky_state.hpp"

#include "astro/atmosphere.hpp"
#include "astro/coordinates.hpp"

#include <algorithm>
#include <cmath>

namespace parallax::astro
{

// =================================================================
// Boundary constants
// =================================================================

static constexpr f32 kDayBoundaryDeg          =   0.0f;
static constexpr f32 kCivilBoundaryDeg        =  -6.0f;
static constexpr f32 kNauticalBoundaryDeg     = -12.0f;
static constexpr f32 kAstroBoundaryDeg        = -18.0f;

// Moon brightness model constant (sprint doc §6.4 simplified model)
static constexpr f32 kMaxFullMoonDelta        =  2.5f;
static constexpr f32 kMoonDeltaClampMax       =  3.5f;

// Limiting magnitude clamp (unphysical values excluded)
static constexpr f32 kLimMagMin               = -10.0f;
static constexpr f32 kLimMagMax               =   8.0f;

// =================================================================
// SkyConditionCalculator::classify
//
// Boundary convention: exact boundaries go to the BRIGHTER state.
//   ≥  0° → Day
//   ≥ -6° → CivilTwilight   (i.e., -6° ≤ alt < 0°)
//   ≥-12° → NauticalTwilight
//   ≥-18° → AstroTwilight
//   < -18° → Night
// =================================================================

SkyState SkyConditionCalculator::classify(f32 sun_alt_deg)
{
    if (sun_alt_deg >= kDayBoundaryDeg)       return SkyState::Day;
    if (sun_alt_deg >= kCivilBoundaryDeg)     return SkyState::CivilTwilight;
    if (sun_alt_deg >= kNauticalBoundaryDeg)  return SkyState::NauticalTwilight;
    if (sun_alt_deg >= kAstroBoundaryDeg)     return SkyState::AstroTwilight;
    return SkyState::Night;
}

// =================================================================
// moon_brightness_delta (private)
//
// Simplified model (sprint doc §6.4):
//   If moon below horizon OR illumination ≤ 0: zero contribution.
//   Otherwise:
//     delta = kMaxFullMoonDelta × illumination × sin(moon_alt_rad)
//   Clamped to [0, kMoonDeltaClampMax].
// =================================================================

f32 SkyConditionCalculator::moon_brightness_delta(f32 moon_alt_deg, f32 moon_illumination)
{
    if (moon_alt_deg <= 0.0f || moon_illumination <= 0.0f)
    {
        return 0.0f;
    }

    const f32 moon_alt_rad = moon_alt_deg * static_cast<f32>(astro_constants::kDegToRad);
    const f32 delta = kMaxFullMoonDelta * moon_illumination * std::sin(moon_alt_rad);

    return std::clamp(delta, 0.0f, kMoonDeltaClampMax);
}

// =================================================================
// twilight_magnitude_penalty (private)
//
// Returns the magnitude REDUCTION to subtract from the base limiting mag.
// Positive value → fewer stars visible.
//
// Day:              10.0 (no stars)
// CivilTwilight:    6.0 → 2.0 as Sun descends from 0° to -6°
// NauticalTwilight: 4.0 → 1.0 as Sun descends from -6° to -12°
// AstroTwilight:    1.0 → 0.0 as Sun descends from -12° to -18°
// Night:            0.0
// =================================================================

f32 SkyConditionCalculator::twilight_magnitude_penalty(SkyState state, f32 sun_alt_deg)
{
    switch (state)
    {
        case SkyState::Day:
            return 10.0f;

        case SkyState::CivilTwilight:
            // Smooth 6→2 across [-6°, 0°]. At 0°: 6.0; at -6°: 2.0.
            return 6.0f - (0.0f - sun_alt_deg) * (4.0f / 6.0f);

        case SkyState::NauticalTwilight:
            // Smooth 4→1 across [-12°, -6°]. At -6°: 4.0; at -12°: 1.0.
            return 4.0f - (-6.0f - sun_alt_deg) * (3.0f / 6.0f);

        case SkyState::AstroTwilight:
            // Smooth 1→0 across [-18°, -12°]. At -12°: 1.0; at -18°: 0.0.
            return 1.0f - (-12.0f - sun_alt_deg) * (1.0f / 6.0f);

        case SkyState::Night:
            return 0.0f;
    }
    return 0.0f;
}

// =================================================================
// SkyConditionCalculator::compute
//
// Algorithm (sprint doc §6.4):
//  1. Convert sun/moon equatorial → horizontal.
//  2. Classify sky state from sun altitude.
//  3. Compute moon sky-brightness delta.
//  4. sky_brightness_zenith = atmosphere.sky_brightness_zenith() - moon_delta.
//     (Lower mag/arcsec² = brighter sky. Moonlight lowers the number.)
//  5. base_lm = atmosphere.limiting_magnitude(π/2)  [zenith reference]
//     lm = base_lm - moon_delta - twilight_penalty
//     lm clamped to [-10, 8].
//  6. Return populated SkyConditions.
//
// Day-state note: sky_brightness_zenith becomes numerically small/negative
// during full daylight (no physical meaning for naked-eye observing).
// Rendering handles the bright-blue daytime sky in its own shader.
// =================================================================

SkyConditions SkyConditionCalculator::compute(
    const CelestialBodyState& sun,
    const CelestialBodyState& moon,
    const ObserverLocation&   observer,
    f64                       lst_rad,
    const Atmosphere&         atmosphere)
{
    // Step 1 — horizontal conversion
    const HorizontalCoord sun_h  = Coordinates::equatorial_to_horizontal(
        sun.equatorial,  observer, lst_rad);
    const HorizontalCoord moon_h = Coordinates::equatorial_to_horizontal(
        moon.equatorial, observer, lst_rad);

    const f32 sun_alt_deg  = static_cast<f32>(sun_h.alt  * astro_constants::kRadToDeg);
    const f32 moon_alt_deg = static_cast<f32>(moon_h.alt * astro_constants::kRadToDeg);

    // Step 2 — sky state from sun altitude
    const SkyState state = classify(sun_alt_deg);

    // Step 3 — moon sky-brightness delta (magnitudes of brightening, ≥ 0)
    const f32 moon_illum  = moon.illumination;
    const f32 moon_delta  = moon_brightness_delta(moon_alt_deg, moon_illum);

    // Step 4 — zenith sky brightness (including Moon)
    // Moonlight brightens the sky → LOWER mag/arcsec² value
    const f32 base_sb          = atmosphere.sky_brightness_zenith();
    const f32 sky_brightness_z = base_sb - moon_delta;

    // Step 5 — effective limiting magnitude
    // Base: zenith limit from Bortle model (no extinction at zenith)
    const f32 base_lm = atmosphere.limiting_magnitude(astro_constants::kHalfPi);
    const f32 penalty = twilight_magnitude_penalty(state, sun_alt_deg);
    const f32 lm_raw  = base_lm - moon_delta - penalty;
    const f32 lm      = std::clamp(lm_raw, kLimMagMin, kLimMagMax);

    return SkyConditions{
        .state                    = state,
        .sun_altitude_deg         = sun_alt_deg,
        .moon_altitude_deg        = moon_alt_deg,
        .moon_illumination        = moon_illum,
        .moon_sky_brightness_delta = moon_delta,
        .effective_limiting_mag   = lm,
        .sky_brightness_zenith    = sky_brightness_z,
    };
}

} // namespace parallax::astro
