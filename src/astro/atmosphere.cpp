/// @file atmosphere.cpp
/// @brief Atmospheric model implementation — refraction, extinction, airmass, sky brightness.

#include "astro/atmosphere.hpp"

#include <algorithm>
#include <cmath>

namespace parallax::astro
{

// =================================================================
// Construction
// =================================================================

Atmosphere::Atmosphere(const AtmosphereParams& params)
    : m_params{params}
{
}

// =================================================================
// Refraction — Bennett formula
//
//   R = 1 / tan(h + 7.31 / (h + 4.4))   [arcminutes, h in degrees]
//   R_corrected = R × (P / 1010) × (283 / (273 + T))
//   Return R in radians.
//
//   For h < 0°: clamp to the horizon value (h = 0).
// =================================================================

f64 Atmosphere::refraction(f64 true_altitude_rad) const
{
    // Clamp negative altitudes to horizon
    const f64 alt_rad = std::max(true_altitude_rad, 0.0);

    // Convert to degrees for the Bennett formula
    const f64 h_deg = alt_rad * astro_constants::kRadToDeg;

    // Bennett formula: R in arcminutes
    // The argument to tan is in degrees — must convert to radians for std::tan
    const f64 tan_arg_deg = h_deg + 7.31 / (h_deg + 4.4);
    const f64 tan_arg_rad = tan_arg_deg * astro_constants::kDegToRad;
    const f64 r_arcmin = 1.0 / std::tan(tan_arg_rad);

    // Pressure and temperature correction
    const f64 pressure_factor = static_cast<f64>(m_params.pressure_mbar) / 1010.0;
    const f64 temp_factor = 283.0 / (273.0 + static_cast<f64>(m_params.temperature_c));
    const f64 r_corrected_arcmin = r_arcmin * pressure_factor * temp_factor;

    // Convert arcminutes → radians
    // 1 arcminute = �� / (180 × 60) radians
    constexpr f64 kArcMinToRad = astro_constants::kDegToRad / 60.0;
    const f64 r_rad = r_corrected_arcmin * kArcMinToRad;

    // Refraction is always positive (lifts objects upward)
    return std::max(r_rad, 0.0);
}

// =================================================================
// Airmass — Rozenberg (1966) formula
//
//   X = 1 / (cos(z) + 0.025 × exp(-11 × cos(z)))
//   where z = π/2 - altitude (zenith angle)
//
//   For alt < 0°: return 40.0 (maximum airmass at/below horizon)
// =================================================================

f64 Atmosphere::airmass(f64 true_altitude_rad) const
{
    if (true_altitude_rad < 0.0)
    {
        return 40.0;
    }

    const f64 z = astro_constants::kHalfPi - true_altitude_rad;
    const f64 cos_z = std::cos(z);

    return 1.0 / (cos_z + 0.025 * std::exp(-11.0 * cos_z));
}

// =================================================================
// Extinction in magnitudes
//
//   Δm = k × X
// =================================================================

f32 Atmosphere::extinction_mag(f64 true_altitude_rad) const
{
    const f64 x = airmass(true_altitude_rad);
    return static_cast<f32>(static_cast<f64>(m_params.extinction_coeff) * x);
}

// =================================================================
// Extinction as linear brightness factor
//
//   factor = 10^(-0.4 × Δm)
// =================================================================

f32 Atmosphere::extinction_factor(f64 true_altitude_rad) const
{
    const f32 delta_m = extinction_mag(true_altitude_rad);
    return static_cast<f32>(std::pow(10.0, -0.4 * static_cast<f64>(delta_m)));
}

// =================================================================
// Sky surface brightness at zenith (mag/arcsec²)
//
// Linear interpolation from the Bortle scale table midpoints:
//   Bortle 1 → 22.0 mag/arcsec²
//   Bortle 9 → 17.0 mag/arcsec²
//
// (Lower number = brighter sky = worse for observing)
// =================================================================

f32 Atmosphere::sky_brightness_zenith() const
{
    // Bortle 1 = 22.0, Bortle 9 = 17.0
    // Linear interpolation: SB = 22.0 - (bortle - 1) × (22.0 - 17.0) / 8
    //                        SB = 22.0 - (bortle - 1) × 0.625
    const f32 bortle_clamped = std::clamp(m_params.bortle_scale, 1.0f, 9.0f);
    return 22.0f - (bortle_clamped - 1.0f) * 0.625f;
}

// =================================================================
// Naked-eye limiting magnitude at given altitude
//
// Zenith limiting magnitude from Bortle scale:
//   Bortle 1 → 7.8 mag
//   Bortle 9 → 3.8 mag
//
// At lower altitudes, extinction reduces the limiting magnitude:
//   m_lim = m_lim_zenith - k × (X - 1)
// =================================================================

f32 Atmosphere::limiting_magnitude(f64 true_altitude_rad) const
{
    // Zenith limiting magnitude from Bortle scale
    // Bortle 1 = 7.8, Bortle 9 = 3.8
    // Linear interpolation: m_lim = 7.8 - (bortle - 1) × (7.8 - 3.8) / 8
    //                        m_lim = 7.8 - (bortle - 1) × 0.5
    const f32 bortle_clamped = std::clamp(m_params.bortle_scale, 1.0f, 9.0f);
    const f32 m_lim_zenith = 7.8f - (bortle_clamped - 1.0f) * 0.5f;

    // Reduce by extinction at this altitude relative to zenith
    const f64 x = airmass(true_altitude_rad);
    const f32 extinction_penalty = m_params.extinction_coeff * static_cast<f32>(x - 1.0);

    return m_lim_zenith - extinction_penalty;
}

// =================================================================
// Parameter access
// =================================================================

void Atmosphere::set_params(const AtmosphereParams& params)
{
    m_params = params;
}

const AtmosphereParams& Atmosphere::get_params() const
{
    return m_params;
}

} // namespace parallax::astro