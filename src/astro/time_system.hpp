#pragma once

/// @file time_system.hpp
/// @brief Astronomical time utilities: Julian Date, sidereal time.

#include "core/types.hpp"

namespace parallax::astro
{
    /// @brief Civil date/time representation (UTC).
    struct DateTime
    {
        i32 year;
        i32 month;
        i32 day;
        i32 hour;
        i32 minute;
        f64 second;
    };

    /// @brief Static utility class for astronomical time computations.
    ///
    /// Provides Julian Date conversion (Meeus algorithm, Astronomical Algorithms Ch. 7),
    /// Greenwich/Local Mean Sidereal Time (IAU 1982), and system clock access.
    /// All angular results are in radians unless noted otherwise.
    class TimeSystem
    {
    public:
        TimeSystem() = delete;

        /// @brief Convert civil date/time (UTC) to Julian Date.
        /// @param dt Civil date/time. Month in [1,12], day in [1,31].
        /// @return Julian Date as a double-precision floating-point number.
        [[nodiscard]] static f64 to_julian_date(const DateTime& dt);

        /// @brief Convert Julian Date back to civil date/time (UTC).
        /// @param jd Julian Date (must be positive).
        /// @return Corresponding civil date/time.
        [[nodiscard]] static DateTime from_julian_date(f64 jd);

        /// @brief Compute Julian centuries elapsed since J2000.0.
        /// @param jd Julian Date.
        /// @return T = (JD - 2451545.0) / 36525.0
        [[nodiscard]] static f64 julian_centuries(f64 jd);

        /// @brief Greenwich Mean Sidereal Time (radians).
        /// @param jd Julian Date (UTC).
        /// @return GMST in radians, normalized to [0, 2π).
        /// Uses the IAU 1982 formula (accurate to ~0.1 second of time).
        [[nodiscard]] static f64 gmst(f64 jd);

        /// @brief Local Mean Sidereal Time (radians).
        /// @param jd Julian Date (UTC).
        /// @param longitude_rad Observer longitude in radians (east positive).
        /// @return LMST in radians, normalized to [0, 2π).
        [[nodiscard]] static f64 lmst(f64 jd, f64 longitude_rad);

        /// @brief Get current system time as a Julian Date.
        /// @return Julian Date corresponding to the current UTC system clock.
        [[nodiscard]] static f64 now_as_jd();

    private:
        /// @brief Normalize an angle to the range [0, 2π).
        [[nodiscard]] static f64 normalize_radians(f64 angle);
    };

} // namespace parallax::astro