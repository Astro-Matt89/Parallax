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
#include <string_view>

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

    /// @brief Named lunar phase.
    enum class MoonPhase : u8
    {
        New,
        WaxingCrescent,
        FirstQuarter,
        WaxingGibbous,
        Full,
        WaningGibbous,
        LastQuarter,
        WaningCrescent,
    };

    /// @brief Get a human-readable name for a MoonPhase value.
    [[nodiscard]] constexpr std::string_view moon_phase_name(MoonPhase phase)
    {
        switch (phase)
        {
            case MoonPhase::New:             return "New";
            case MoonPhase::WaxingCrescent:  return "Waxing Crescent";
            case MoonPhase::FirstQuarter:    return "First Quarter";
            case MoonPhase::WaxingGibbous:   return "Waxing Gibbous";
            case MoonPhase::Full:            return "Full";
            case MoonPhase::WaningGibbous:   return "Waning Gibbous";
            case MoonPhase::LastQuarter:     return "Last Quarter";
            case MoonPhase::WaningCrescent:  return "Waning Crescent";
        }
        return "Unknown";
    }

    /// @brief Extended Moon state including phase information.
    struct MoonState
    {
        CelestialBodyState body;        ///< Standard body state (position, mag, etc.)
        MoonPhase phase;                ///< Named phase (New, Full, etc.)
        f64 elongation_deg;             ///< Angular elongation from Sun (degrees)
        f64 distance_km;                ///< Geocentric distance in km
    };

    /// @brief Keplerian orbital elements with polynomial coefficients (Meeus Table 31.A).
    ///
    /// Each element is expressed as a polynomial in T (Julian centuries from J2000.0):
    ///   value = c[0] + c[1]*T + c[2]*T² + c[3]*T³
    ///
    /// Referred to the mean ecliptic and equinox of J2000.0.
    struct OrbitalElements
    {
        f64 L[4];       ///< Mean longitude (degrees)
        f64 a[2];       ///< Semi-major axis (AU): constant + rate per century
        f64 e[4];       ///< Eccentricity
        f64 i[4];       ///< Inclination (degrees)
        f64 omega[4];   ///< Longitude of ascending node Ω (degrees)
        f64 pi[4];      ///< Longitude of perihelion ϖ (degrees)
    };

    /// @brief Planet identifier constants.
    ///
    /// Values match traditional numbering (Earth=3 skipped).
    namespace planet_id
    {
        constexpr u32 kMercury = 1;
        constexpr u32 kVenus   = 2;
        constexpr u32 kMars    = 4;
        constexpr u32 kJupiter = 5;
        constexpr u32 kSaturn  = 6;
        constexpr u32 kUranus  = 7;
        constexpr u32 kNeptune = 8;
    } // namespace planet_id

    /// @brief Heliocentric ecliptic Cartesian position.
    struct HeliocentricPos
    {
        f64 x;  ///< AU, toward vernal equinox
        f64 y;  ///< AU, 90° east in ecliptic plane
        f64 z;  ///< AU, toward ecliptic north pole
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

        /// @brief Compute Moon position for a given Julian Date.
        ///
        /// Uses Meeus Ch. 47 abridged algorithm (~0.1° accuracy).
        /// Top 15 longitude terms, top 10 latitude terms, top 10 distance terms.
        ///
        /// @param jd Julian Date.
        /// @return Moon state with RA/Dec, distance, magnitude, angular diameter.
        [[nodiscard]] static CelestialBodyState compute_moon(f64 jd);

        /// @brief Compute extended Moon state including phase information.
        ///
        /// Calls compute_moon() and compute_sun() internally, then derives
        /// elongation, phase angle, illumination fraction, and named phase.
        ///
        /// @param jd Julian Date.
        /// @return Extended Moon state with phase, elongation, distance in km.
        [[nodiscard]] static MoonState compute_moon_full(f64 jd);

        /// @brief Compute planet position for a given Julian Date.
        ///
        /// Uses Meeus Ch. 31 Keplerian orbital elements with Kepler's equation.
        /// Accuracy ~0.1–0.5° depending on planet and epoch.
        ///
        /// @param jd Julian Date.
        /// @param planet_id 1=Mercury, 2=Venus, 4=Mars, 5=Jupiter, 6=Saturn, 7=Uranus, 8=Neptune.
        /// @return Planet state with RA/Dec, distance, magnitude, phase angle.
        [[nodiscard]] static CelestialBodyState compute_planet(f64 jd, u32 planet_id);

        /// @brief All Solar System bodies computed at once (shared Earth position).
        struct AllBodies
        {
            CelestialBodyState sun;
            CelestialBodyState moon;
            std::array<CelestialBodyState, 7> planets;  ///< Mercury..Neptune (no Earth)
        };

        /// @brief Compute all bodies at once (more efficient — shares Earth's heliocentric position).
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

        /// @brief Compute heliocentric ecliptic position of a planet from orbital elements.
        ///
        /// @param elements Orbital elements for the planet.
        /// @param T Julian centuries from J2000.0.
        /// @return Heliocentric ecliptic Cartesian coordinates (AU).
        [[nodiscard]] static HeliocentricPos compute_heliocentric(
            const OrbitalElements& elements, f64 T);

        /// @brief Compute heliocentric ecliptic position of Earth.
        ///
        /// @param T Julian centuries from J2000.0.
        /// @return Earth's heliocentric ecliptic Cartesian coordinates (AU).
        [[nodiscard]] static HeliocentricPos compute_earth_heliocentric(f64 T);

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

        /// @brief Determine waxing/waning from mean elongation.
        ///
        /// @param elongation_deg Angular elongation from Sun (0..360).
        /// @param illumination Illumination fraction (0..1).
        /// @return Named MoonPhase.
        [[nodiscard]] static MoonPhase classify_moon_phase(
            f64 elongation_deg, f64 illumination);

        /// @brief Solve Kepler's equation M = E - e*sin(E) iteratively.
        ///
        /// @param M_rad Mean anomaly (radians).
        /// @param e Eccentricity.
        /// @return Eccentric anomaly E (radians).
        [[nodiscard]] static f64 solve_kepler(f64 M_rad, f64 e);

        /// @brief Compute planet state from heliocentric positions.
        ///
        /// Given a planet's and Earth's heliocentric positions, computes
        /// geocentric ecliptic → equatorial coordinates, distance, phase angle,
        /// and apparent magnitude.
        ///
        /// @param planet Heliocentric position of the planet.
        /// @param earth Heliocentric position of Earth.
        /// @param r_helio Heliocentric distance of the planet (AU).
        /// @param planet_id Planet identifier for magnitude computation.
        /// @param eps_rad Obliquity of the ecliptic (radians).
        /// @return Complete planet state.
        [[nodiscard]] static CelestialBodyState compute_planet_state(
            const HeliocentricPos& planet,
            const HeliocentricPos& earth,
            f64 r_helio,
            u32 planet_id,
            f64 eps_rad);

        /// @brief Compute apparent magnitude for a planet.
        ///
        /// Uses simplified formulas from Meeus Ch. 41 / sprint doc.
        ///
        /// @param planet_id Planet identifier.
        /// @param r Heliocentric distance (AU).
        /// @param delta Geocentric distance (AU).
        /// @param phase_angle_deg Phase angle (degrees).
        /// @return Apparent visual magnitude.
        [[nodiscard]] static f32 compute_planet_magnitude(
            u32 planet_id, f64 r, f64 delta, f64 phase_angle_deg);

        /// @brief Get the angular diameter of a planet at 1 AU, in arcseconds.
        [[nodiscard]] static f64 planet_angular_diameter_at_1au(u32 planet_id);

        /// @brief Get orbital elements for a given planet ID.
        /// @return Pointer to elements, or nullptr for invalid ID.
        [[nodiscard]] static const OrbitalElements* get_planet_elements(u32 planet_id);
    }; 
} // namespace parallax::astro