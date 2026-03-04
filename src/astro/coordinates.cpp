/// @file coordinates.cpp
/// @brief Implementation of astronomical coordinate transformations.

#include "astro/coordinates.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace parallax::astro
{

// -----------------------------------------------------------------
// Equatorial (RA/Dec) → Horizontal (Alt/Az)
//
// Hour angle: H = LST - RA
//
// sin(alt) = sin(dec) × sin(lat) + cos(dec) × cos(lat) × cos(H)
//
// Azimuth (north-based):
//   sin(az) × cos(alt) = -cos(dec) × sin(H)
//   cos(az) × cos(alt) =  sin(dec) × cos(lat) - cos(dec) × sin(lat) × cos(H)
//   az = atan2(-cos(dec)×sin(H), sin(dec)×cos(lat) - cos(dec)×sin(lat)×cos(H))
//
// Normalize az to [0, 2π)
// -----------------------------------------------------------------

HorizontalCoord Coordinates::equatorial_to_horizontal(
    const EquatorialCoord& eq,
    const ObserverLocation& observer,
    f64 local_sidereal_time_rad)
{
    const f64 hour_angle = local_sidereal_time_rad - eq.ra;

    const f64 sin_dec = std::sin(eq.dec);
    const f64 cos_dec = std::cos(eq.dec);
    const f64 sin_lat = std::sin(observer.latitude_rad);
    const f64 cos_lat = std::cos(observer.latitude_rad);
    const f64 cos_ha  = std::cos(hour_angle);
    const f64 sin_ha  = std::sin(hour_angle);

    // Altitude
    const f64 sin_alt = sin_dec * sin_lat + cos_dec * cos_lat * cos_ha;
    const f64 alt = std::asin(std::clamp(sin_alt, -1.0, 1.0));

    // Azimuth (north-based, east = π/2)
    const f64 az_y = -cos_dec * sin_ha;
    const f64 az_x = sin_dec * cos_lat - cos_dec * sin_lat * cos_ha;
    f64 az = std::atan2(az_y, az_x);

    az = normalize_radians(az);

    return HorizontalCoord{
        .alt = alt,
        .az  = az,
    };
}

// -----------------------------------------------------------------
// Horizontal (Alt/Az) → Equatorial (RA/Dec)
// -----------------------------------------------------------------

EquatorialCoord Coordinates::horizontal_to_equatorial(
    const HorizontalCoord& hz,
    const ObserverLocation& observer,
    f64 local_sidereal_time_rad)
{
    const f64 sin_alt = std::sin(hz.alt);
    const f64 cos_alt = std::cos(hz.alt);
    const f64 sin_az  = std::sin(hz.az);
    const f64 cos_az  = std::cos(hz.az);
    const f64 sin_lat = std::sin(observer.latitude_rad);
    const f64 cos_lat = std::cos(observer.latitude_rad);

    // Declination
    const f64 sin_dec = sin_alt * sin_lat + cos_alt * cos_lat * cos_az;
    const f64 dec = std::asin(std::clamp(sin_dec, -1.0, 1.0));

    // Hour angle
    const f64 ha_y = -cos_alt * sin_az;
    const f64 ha_x = sin_alt * cos_lat - cos_alt * sin_lat * cos_az;
    const f64 hour_angle = std::atan2(ha_y, ha_x);

    // Right ascension
    const f64 ra = normalize_radians(local_sidereal_time_rad - hour_angle);

    return EquatorialCoord{
        .ra  = ra,
        .dec = dec,
    };
}

// -----------------------------------------------------------------
// Horizontal (Alt/Az) → Gnomonic screen projection
// -----------------------------------------------------------------

std::optional<Vec2f> Coordinates::horizontal_to_screen(
    const HorizontalCoord& star,
    const HorizontalCoord& pointing,
    f64 fov_rad)
{
    const f64 delta_az = star.az - pointing.az;

    const f64 sin_alt_s = std::sin(star.alt);
    const f64 cos_alt_s = std::cos(star.alt);
    const f64 sin_alt_p = std::sin(pointing.alt);
    const f64 cos_alt_p = std::cos(pointing.alt);
    const f64 cos_daz   = std::cos(delta_az);
    const f64 sin_daz   = std::sin(delta_az);

    // Angular separation via dot product of unit vectors
    const f64 cos_sep = sin_alt_s * sin_alt_p + cos_alt_s * cos_alt_p * cos_daz;

    // Clamp for numerical safety, then check FOV boundary
    const f64 separation = std::acos(std::clamp(cos_sep, -1.0, 1.0));

    // Use a generous margin: screen diagonal of a square FOV is FOV * sqrt(2)/2.
    // We use 0.75 as a safe margin to avoid clipping corners.
    if (separation > fov_rad * 0.75)
    {
        return std::nullopt;
    }

    // Guard against division by zero (star directly behind the observer)
    if (cos_sep <= 0.0)
    {
        return std::nullopt;
    }

    // Gnomonic tangent-plane projection
    const f64 dx = cos_alt_s * sin_daz;
    const f64 dy = sin_alt_s * cos_alt_p - cos_alt_s * sin_alt_p * cos_daz;

    const f64 proj_x = dx / cos_sep;
    const f64 proj_y = dy / cos_sep;

    // Scale: normalize so that FOV/2 maps to screen edge (±1)
    const f64 scale = 1.0 / std::tan(fov_rad * 0.5);

    // +X = East (right on screen in observer's view)
    // -Y = Vulkan NDC correction: negate so higher altitude = screen up
    const auto screen_x = static_cast<f32>( proj_x * scale);
    const auto screen_y = static_cast<f32>(-proj_y * scale);

    // Final bounds check in normalized screen space
    if (std::abs(screen_x) > 1.0f || std::abs(screen_y) > 1.0f)
    {
        return std::nullopt;
    }

    return Vec2f{screen_x, screen_y};
}

// -----------------------------------------------------------------
// Full RA/Dec → screen NDC — SHARED projection pipeline       ← SPRINT 04
//
// This is THE canonical function for going from catalog RA/Dec to
// screen position. Both the starfield and all overlays MUST use it
// (or call the same sub-steps in the same order).
// -----------------------------------------------------------------

std::optional<Vec2f> Coordinates::project_radec_to_screen(
    f64 ra_rad,
    f64 dec_rad,
    const ObserverLocation& observer,
    f64 lst_rad,
    const HorizontalCoord& pointing,
    f64 fov_rad)
{
    // Step 1: RA/Dec → Alt/Az
    const EquatorialCoord eq{.ra = ra_rad, .dec = dec_rad};
    const auto hz = equatorial_to_horizontal(eq, observer, lst_rad);

    // Step 2: Horizon cull
    if (hz.alt < 0.0)
    {
        return std::nullopt;
    }

    // Step 3: Alt/Az → screen NDC
    return horizontal_to_screen(hz, pointing, fov_rad);
}

// -----------------------------------------------------------------
// Normalize angle to [0, 2π)
// -----------------------------------------------------------------

f64 Coordinates::normalize_radians(f64 angle)
{
    angle = std::fmod(angle, astro_constants::kTwoPi);
    if (angle < 0.0)
    {
        angle += astro_constants::kTwoPi;
    }
    return angle;
}

} // namespace parallax::astro