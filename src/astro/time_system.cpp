/// @file time_system.cpp
/// @brief Implementation of astronomical time utilities.

#include "astro/time_system.hpp"

#include "core/types.hpp"

#include <cmath>
#include <chrono>

namespace parallax::astro
{

// -----------------------------------------------------------------
// Julian Date — Meeus algorithm (Astronomical Algorithms, Ch. 7)
// -----------------------------------------------------------------

f64 TimeSystem::to_julian_date(const DateTime& dt)
{
    i32 y = dt.year;
    i32 m = dt.month;

    // Jan and Feb are treated as months 13 and 14 of the previous year
    if (m <= 2)
    {
        y -= 1;
        m += 12;
    }

    // Gregorian calendar correction
    const i32 a = y / 100;
    const i32 b = 2 - a + (a / 4);

    // Day fraction from hours, minutes, seconds
    const f64 day_fraction = (static_cast<f64>(dt.hour)
                            + static_cast<f64>(dt.minute) / 60.0
                            + dt.second / 3600.0) / 24.0;

    const f64 jd = std::floor(365.25 * static_cast<f64>(y + 4716))
                 + std::floor(30.6001 * static_cast<f64>(m + 1))
                 + static_cast<f64>(dt.day)
                 + day_fraction
                 + static_cast<f64>(b)
                 - 1524.5;

    return jd;
}

// -----------------------------------------------------------------
// Julian Date → civil date/time (Meeus, Ch. 7)
// -----------------------------------------------------------------

DateTime TimeSystem::from_julian_date(f64 jd)
{
    // Add 0.5 to shift from noon-based to midnight-based
    const f64 jd_plus = jd + 0.5;
    const i32 z = static_cast<i32>(std::floor(jd_plus));
    const f64 f = jd_plus - static_cast<f64>(z);

    i32 a = z;
    if (z >= 2299161)
    {
        const i32 alpha = static_cast<i32>(std::floor(
            (static_cast<f64>(z) - 1867216.25) / 36524.25));
        a = z + 1 + alpha - (alpha / 4);
    }

    const i32 b = a + 1524;
    const i32 c = static_cast<i32>(std::floor(
        (static_cast<f64>(b) - 122.1) / 365.25));
    const i32 d = static_cast<i32>(std::floor(
        365.25 * static_cast<f64>(c)));
    const i32 e = static_cast<i32>(std::floor(
        static_cast<f64>(b - d) / 30.6001));

    // Day (with fractional part)
    const f64 day_with_fraction = static_cast<f64>(b - d)
                                - std::floor(30.6001 * static_cast<f64>(e))
                                + f;

    const i32 day = static_cast<i32>(std::floor(day_with_fraction));
    const f64 day_frac = day_with_fraction - static_cast<f64>(day);

    // Month
    i32 month = (e < 14) ? (e - 1) : (e - 13);

    // Year
    i32 year = (month > 2) ? (c - 4716) : (c - 4715);

    // Time from day fraction
    const f64 hours_total = day_frac * 24.0;
    const i32 hour = static_cast<i32>(std::floor(hours_total));

    const f64 minutes_total = (hours_total - static_cast<f64>(hour)) * 60.0;
    const i32 minute = static_cast<i32>(std::floor(minutes_total));

    const f64 second = (minutes_total - static_cast<f64>(minute)) * 60.0;

    return DateTime{
        .year   = year,
        .month  = month,
        .day    = day,
        .hour   = hour,
        .minute = minute,
        .second = second,
    };
}

// -----------------------------------------------------------------
// Julian centuries since J2000.0
// -----------------------------------------------------------------

f64 TimeSystem::julian_centuries(f64 jd)
{
    return (jd - astro_constants::kJ2000) / 36525.0;
}

// -----------------------------------------------------------------
// GMST — IAU 1982 formula
//
// GMST (degrees) = 280.46061837
//                + 360.98564736629 × (JD − 2451545.0)
//                + 0.000387933 × T²
//                − T³ / 38710000
//
// Where T = Julian centuries from J2000.0
// Result normalized to [0, 360°), then converted to radians.
// -----------------------------------------------------------------

f64 TimeSystem::gmst(f64 jd)
{
    const f64 t = julian_centuries(jd);
    const f64 d = jd - astro_constants::kJ2000;

    // GMST in degrees
    f64 gmst_deg = 280.46061837
                 + 360.98564736629 * d
                 + 0.000387933 * t * t
                 - (t * t * t) / 38710000.0;

    // Normalize to [0, 360)
    gmst_deg = std::fmod(gmst_deg, 360.0);
    if (gmst_deg < 0.0)
    {
        gmst_deg += 360.0;
    }

    return gmst_deg * astro_constants::kDegToRad;
}

// -----------------------------------------------------------------
// LMST = GMST + observer longitude
// -----------------------------------------------------------------

f64 TimeSystem::lmst(f64 jd, f64 longitude_rad)
{
    return normalize_radians(gmst(jd) + longitude_rad);
}

// -----------------------------------------------------------------
// Current system time → Julian Date
// -----------------------------------------------------------------

f64 TimeSystem::now_as_jd()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto since_epoch = now.time_since_epoch();
    const auto total_seconds = duration_cast<duration<f64>>(since_epoch).count();

    // Unix epoch (1970-01-01 00:00 UTC) as Julian Date
    constexpr f64 kUnixEpochJd = 2440587.5;

    return kUnixEpochJd + total_seconds / 86400.0;
}

// -----------------------------------------------------------------
// Normalize angle to [0, 2π)
// -----------------------------------------------------------------

f64 TimeSystem::normalize_radians(f64 angle)
{
    angle = std::fmod(angle, astro_constants::kTwoPi);
    if (angle < 0.0)
    {
        angle += astro_constants::kTwoPi;
    }
    return angle;
}

} // namespace parallax::astro