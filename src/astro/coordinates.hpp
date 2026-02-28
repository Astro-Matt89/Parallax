#pragma once

/// @file coordinates.hpp
/// @brief Astronomical coordinate transforms: Equatorial, Horizontal, Screen projection.

#include "core/types.hpp"

#include <optional>

namespace parallax::astro
{
    /// @brief Equatorial coordinate (J2000 epoch).
    struct EquatorialCoord
    {
        f64 ra;     ///< Right ascension (radians, 0..2π)
        f64 dec;    ///< Declination (radians, -π/2..+π/2)
    };

    /// @brief Horizontal (topocentric) coordinate.
    struct HorizontalCoord
    {
        f64 alt;    ///< Altitude (radians, -π/2..+π/2, negative = below horizon)
        f64 az;     ///< Azimuth (radians, 0..2π, 0=North, π/2=East)
    };

    /// @brief Observer geographic location.
    struct ObserverLocation
    {
        f64 latitude_rad;   ///< Geographic latitude (radians, north positive)
        f64 longitude_rad;  ///< Geographic longitude (radians, east positive)
    };

    /// @brief Static utility class for astronomical coordinate transformations.
    ///
    /// All angular inputs and outputs are in radians.
    /// Double precision (f64) is used throughout for arcsecond-level accuracy.
    class Coordinates
    {
    public:
        Coordinates() = delete;

        /// @brief Equatorial (RA/Dec J2000) → Horizontal (Alt/Az).
        /// @param eq Equatorial coordinates of the object.
        /// @param observer Observer geographic location.
        /// @param local_sidereal_time_rad Local Mean Sidereal Time (radians).
        /// @return Horizontal coordinates (altitude and azimuth).
        [[nodiscard]] static HorizontalCoord equatorial_to_horizontal(
            const EquatorialCoord& eq,
            const ObserverLocation& observer,
            f64 local_sidereal_time_rad
        );

        /// @brief Horizontal (Alt/Az) → Equatorial (RA/Dec).
        /// @param hz Horizontal coordinates of the object.
        /// @param observer Observer geographic location.
        /// @param local_sidereal_time_rad Local Mean Sidereal Time (radians).
        /// @return Equatorial coordinates (right ascension and declination).
        [[nodiscard]] static EquatorialCoord horizontal_to_equatorial(
            const HorizontalCoord& hz,
            const ObserverLocation& observer,
            f64 local_sidereal_time_rad
        );

        /// @brief Horizontal (Alt/Az) → Stereographic screen projection.
        ///
        /// Projects a star's horizontal position onto a 2D screen plane
        /// centered on the camera pointing direction.
        ///
        /// @param star Horizontal coordinates of the star.
        /// @param pointing Horizontal coordinates of the camera center.
        /// @param fov_rad Field of view in radians (full angular width).
        /// @return Normalized screen coordinates in [-1, 1], or std::nullopt if off screen.
        [[nodiscard]] static std::optional<Vec2f> horizontal_to_screen(
            const HorizontalCoord& star,
            const HorizontalCoord& pointing,
            f64 fov_rad
        );

    private:
        /// @brief Normalize an angle to the range [0, 2π).
        [[nodiscard]] static f64 normalize_radians(f64 angle);
    };

} // namespace parallax::astro