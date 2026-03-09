#pragma once

/// @file solar_system.hpp
/// @brief Solar System ephemeris: Sun, Moon, and planet positions.
///
/// Implements the low-precision algorithms from Meeus "Astronomical Algorithms"
/// for computing geocentric equatorial coordinates of Solar System bodies.
/// All methods are static — pure computation, no state.

#include "astro/coordinates.hpp"
#include "core/types.hpp"

#include <array>

namespace parallax::astro
{

    /// @brief Complete state of a Solar System body at a given instant.
    struct CelestialBodyState
    {
        EquatorialCoord equatorial;     ///< RA/Dec (geocentric, apparent)
        HorizontalCoord horizontal;     ///< Alt/Az (observer-dependent, computed separately)
        f64 distance_au;                ///< Distance from Earth (AU)
        f32 magnitude;                  ///< Apparent visual magnitude
        f32 angular_diameter_arcsec;    ///< Apparent angular size
        f32 phase_angle_deg;            ///< Phase angle (Sun-Body-Earth)
        f32 illumination;               ///< Fraction illuminated (0..1)
    };

    /// @brief Solar System ephemeris calculator.
    ///
    /// Provides static methods to compute positions of the Sun, Moon, and
    /// major planets for any Julian Date. All algorithms are from Meeus
    /// "Astronomical Algorithms" — coefficients are embedded as constants.
    ///
    /// This is a pure computation module with no rendering or state dependencies.
    class SolarSystem
    {
    public:
        SolarSystem() = delete;

        /// @brief Compute Sun position for a given Julian Date.
        ///
        /// Uses Meeus Ch. 25 low-precision algorithm (~0.01° accuracy).
        /// Returns geocentric apparent equatorial coordinates.
        ///
        /// @param jd Julian Date (TDT/UTC — difference negligible for this precision).
        /// @return Sun state with RA/Dec, distance, magnitude, angular diameter.
        [[nodiscard]] static CelestialBodyState compute_sun(f64 jd);

        /// @brief Compute Moon position for a given Julian Date (stub — Task 6.2).
        [[nodiscard]] static CelestialBodyState compute_moon(f64 jd);

        /// @brief Compute planet position for a given Julian Date (stub — Task 6.3).
        /// @param planet_id 1=Mercury, 2=Venus, 4=Mars, 5=Jupiter, 6=Saturn, 7=Uranus, 8=Neptune.
        [[nodiscard]] static CelestialBodyState compute_planet(f64 jd, u32 planet_id);

        /// @brief All Solar System bodies computed at once (stubs for Moon/planets).
        struct AllBodies
        {
            CelestialBodyState sun;
            CelestialBodyState moon;
            std::array<CelestialBodyState, 7> planets;  ///< Mercury..Neptune (no Earth)
        };

        /// @brief Compute all bodies at once.
        [[nodiscard]] static AllBodies compute_all(f64 jd);

        // -----------------------------------------------------------------
        // Utility methods (public for reuse by Moon/planet tasks)
        // -----------------------------------------------------------------

        /// @brief Convert ecliptic coordinates to equatorial.
        ///
        /// @param lambda_rad Ecliptic longitude (radians).
        /// @param beta_rad Ecliptic latitude (radians).
        /// @param epsilon_rad Obliquity of the ecliptic (radians).
        /// @return Equatorial coordinates (RA/Dec).
        [[nodiscard]] static EquatorialCoord ecliptic_to_equatorial(
            f64 lambda_rad, f64 beta_rad, f64 epsilon_rad);

        /// @brief Mean obliquity of the ecliptic (Meeus formula).
        ///
        /// @param jd Julian Date.
        /// @return Mean obliquity in radians.
        [[nodiscard]] static f64 mean_obliquity(f64 jd);

    private:
        /// @brief Compute Sun geometric mean longitude, mean anomaly, eccentricity.
        ///
        /// Meeus Ch. 25 intermediate values needed by the solar position algorithm.
        /// All outputs in degrees (caller converts to radians as needed).
        ///
        /// @param jd Julian Date.
        /// @param[out] L0 Geometric mean longitude (degrees).
        /// @param[out] M Mean anomaly (degrees).
        /// @param[out] e Eccentricity of Earth's orbit.
        static void compute_sun_geometric(f64 jd, f64& L0, f64& M, f64& e);

        /// @brief Normalize an angle to [0, 360) degrees.
        [[nodiscard]] static f64 normalize_degrees(f64 angle);
    };

} // namespace parallax::astro
