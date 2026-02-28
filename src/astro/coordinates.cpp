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
//
// Inverse of the above transform:
//   sin(dec) = sin(alt) × sin(lat) + cos(alt) × cos(lat) × cos(az)
//
//   Hour angle:
//     sin(H) × cos(dec) = -cos(alt) × sin(az)
//     cos(H) × cos(dec) =  sin(alt) × cos(lat) - cos(alt) × sin(lat) × cos(az)
//     H = atan2(-cos(alt)×sin(az), sin(alt)×cos(lat) - cos(alt)×sin(lat)×cos(az))
//
//   RA = LST - H, normalized to [0, 2π)
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
// Horizontal (Alt/Az) → Stereographic screen projection
//
// 1. Convert both star and pointing to Cartesian unit vectors
// 2. Compute angular separation via dot product
// 3. If separation > FOV/2: return nullopt (off screen)
// 4. Gnomonic-like tangent-plane projection:
//    dx = cos(alt_star) × sin(Δaz)
//    dy = sin(alt_star) × cos(alt_center) - cos(alt_star) × sin(alt_center) × cos(Δaz)
//    denom = sin(alt_star) × sin(alt_center) + cos(alt_star) × cos(alt_center) × cos(Δaz)
// 5. Scale by 2/FOV to normalize to [-1, 1]
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

    // Use a generous margin (half diagonal ≈ FOV * 0.71) to avoid clipping corners
    // but the sprint spec says FOV/2, so we use a slightly expanded check:
    // screen diagonal of a square FOV is FOV * sqrt(2)/2 ≈ FOV * 0.707
    // We use 0.75 as a safe margin
    if (separation > fov_rad * 0.75)
    {
        return std::nullopt;
    }

    // Tangent-plane projection
    // denom = cos(angular separation) — the dot product we already computed
    // Guard against division by zero (star directly behind the observer)
    if (cos_sep <= 0.0)
    {
        return std::nullopt;
    }

    const f64 dx = cos_alt_s * sin_daz;
    const f64 dy = sin_alt_s * cos_alt_p - cos_alt_s * sin_alt_p * cos_daz;

    // Gnomonic projection: divide by cos_sep for true gnomonic
    const f64 proj_x = dx / cos_sep;
    const f64 proj_y = dy / cos_sep;

    // Scale: at the edge of the FOV, the tangent-plane distance is tan(FOV/2)
    // Normalize so that FOV/2 maps to screen edge (±1)
    const f64 scale = 1.0 / std::tan(fov_rad * 0.5);

    const auto screen_x = static_cast<f32>(proj_x * scale);
    const auto screen_y = static_cast<f32>(proj_y * scale);

    // Final bounds check in normalized screen space
    if (std::abs(screen_x) > 1.0f || std::abs(screen_y) > 1.0f)
    {
        return std::nullopt;
    }

    return Vec2f{screen_x, screen_y};
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