#pragma once

/// @file coordinates.hpp
/// @brief Astronomical coordinate transforms: Equatorial, Horizontal, Screen projection.

#include "astro/observer.hpp"
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
        /// @param fov_rad Field of view in radians (vertical full angular width).
        /// @param aspect_ratio Viewport width / height.
        /// @return Normalized screen coordinates in [-1, 1], or std::nullopt if off screen.
        [[nodiscard]] static std::optional<Vec2f> horizontal_to_screen(
            const HorizontalCoord& star,
            const HorizontalCoord& pointing,
            f64 fov_rad,
            f64 aspect_ratio = 1.0
        );

        /// @brief Full RA/Dec → screen NDC projection pipeline.                ← SPRINT 04
        ///
        /// Single shared function used by BOTH the starfield renderer AND
        /// constellation overlays to guarantee identical screen positions.
        ///
        /// Pipeline:
        ///   1. equatorial_to_horizontal(ra, dec, observer, lst)
        ///   2. Horizon cull: alt < 0 → nullopt
        ///   3. horizontal_to_screen(hz, pointing, fov)
        ///
        /// @param ra_rad Right ascension in radians.
        /// @param dec_rad Declination in radians.
        /// @param observer Observer geographic location.
        /// @param lst_rad Local Mean Sidereal Time in radians.
        /// @param pointing Camera pointing direction (Alt/Az).
        /// @param fov_rad Camera field of view in radians.
        /// @param aspect_ratio Viewport width / height.
        /// @return Screen NDC in [-1, 1], or std::nullopt if below horizon or off screen.
        [[nodiscard]] static std::optional<Vec2f> project_radec_to_screen(
            f64 ra_rad,
            f64 dec_rad,
            const ObserverLocation& observer,
            f64 lst_rad,
            const HorizontalCoord& pointing,
            f64 fov_rad,
            f64 aspect_ratio = 1.0
        );

    private:
        /// @brief Normalize an angle to the range [0, 2π).
        [[nodiscard]] static f64 normalize_radians(f64 angle);
    };

} // namespace parallax::astro