#pragma once

/// @file sky_state.hpp
/// @brief Sky condition calculator: twilight state, Moon contribution, limiting magnitude.
///
/// Determines observing conditions based on Sun altitude, Moon position/phase,
/// and site atmosphere. Delegates Bortle-based sky brightness to the Atmosphere
/// class — does NOT duplicate that logic.
///
/// Twilight thresholds follow USNO definitions.
/// Boundary convention (implemented in classify()): boundaries belong to the
/// BRIGHTER state, matching USNO phrasing ("Civil twilight: Sun alt 0° to -6°"):
///   Day:                Sun alt ≥ 0°
///   Civil twilight:     -6° ≤ Sun alt < 0°
///   Nautical twilight:  -12° ≤ Sun alt < -6°
///   Astro twilight:     -18° ≤ Sun alt < -12°
///   Night:              Sun alt < -18°

#include "astro/coordinates.hpp"
#include "astro/solar_system.hpp"
#include "core/types.hpp"

#include <string_view>

namespace parallax::astro
{
    class Atmosphere;  // forward decl — avoid include cycle

    /// @brief Sky state derived solely from Sun altitude thresholds (USNO).
    enum class SkyState : u8
    {
        Day,                ///< Sun altitude ≥ 0°
        CivilTwilight,      ///< Sun altitude -6° to 0°
        NauticalTwilight,   ///< Sun altitude -12° to -6°
        AstroTwilight,      ///< Sun altitude -18° to -12°
        Night,              ///< Sun altitude < -18°
    };

    /// @brief Get a human-readable name for a SkyState value.
    [[nodiscard]] constexpr std::string_view sky_state_name(SkyState s)
    {
        switch (s)
        {
            case SkyState::Day:              return "DAY";
            case SkyState::CivilTwilight:    return "CIVIL TWI";
            case SkyState::NauticalTwilight: return "NAUTICAL TWI";
            case SkyState::AstroTwilight:    return "ASTRO TWI";
            case SkyState::Night:            return "NIGHT";
        }
        return "UNKNOWN";
    }

    /// @brief Complete snapshot of observable-sky conditions.
    struct SkyConditions
    {
        SkyState state;                     ///< Twilight / day / night classification
        f32 sun_altitude_deg;               ///< Sun altitude above horizon (degrees)
        f32 moon_altitude_deg;              ///< Moon altitude above horizon (degrees)
        f32 moon_illumination;              ///< Moon illumination fraction (0..1)
        f32 moon_sky_brightness_delta;      ///< Sky brightening from Moon in magnitudes (≥ 0)
        f32 effective_limiting_mag;         ///< Naked-eye limiting magnitude after all effects
        f32 sky_brightness_zenith;          ///< mag/arcsec² at zenith (final, incl. Moon)
    };

    /// @brief Computes sky conditions from Sun/Moon states and observer location.
    ///
    /// Pure static computation class — no rendering, no mutable state.
    /// Delegates Bortle-based sky brightness / limiting magnitude to Atmosphere.
    class SkyConditionCalculator
    {
    public:
        SkyConditionCalculator() = delete;

        /// @brief Compute sky conditions for the given instant.
        ///
        /// Converts sun/moon equatorial coords to horizontal internally; the
        /// caller does NOT need to pre-populate sun.horizontal / moon.horizontal.
        ///
        /// @param sun  Sun state from SolarSystem::compute_sun() (geocentric equatorial).
        /// @param moon Moon state from SolarSystem::compute_moon[_full]() (equatorial + illumination).
        /// @param observer Observer geographic location.
        /// @param lst_rad   Local Mean Sidereal Time (radians).
        /// @param atmosphere Atmosphere model providing Bortle-based sky brightness.
        /// @return Complete sky conditions snapshot.
        [[nodiscard]] static SkyConditions compute(
            const CelestialBodyState& sun,
            const CelestialBodyState& moon,
            const ObserverLocation&   observer,
            f64                       lst_rad,
            const Atmosphere&         atmosphere);

        /// @brief Classify sky state from Sun altitude alone.
        ///
        /// Exposed publicly so tests can verify threshold logic without
        /// constructing full CelestialBodyState / Atmosphere objects.
        ///
        /// Boundary convention: boundary values belong to the brighter state
        /// (e.g. exactly 0° → Day; exactly -6° → CivilTwilight).
        ///
        /// @param sun_alt_deg Sun altitude in degrees.
        /// @return Sky state classification.
        [[nodiscard]] static SkyState classify(f32 sun_alt_deg);

    private:
        /// @brief Moon sky-brightness delta in magnitudes (≥ 0).
        ///
        /// Simplified model (sprint 06 §6.4): full moon at zenith ≈ 2.5 mag;
        /// scales linearly with illumination and sin(moon_altitude).
        ///
        /// @param moon_alt_deg     Moon altitude in degrees.
        /// @param moon_illumination Moon illumination fraction (0..1).
        /// @return Sky brightening contribution (0 when Moon below horizon or new).
        [[nodiscard]] static f32 moon_brightness_delta(
            f32 moon_alt_deg, f32 moon_illumination);

        /// @brief Sun twilight penalty on limiting magnitude (positive = subtract).
        ///
        /// @param state       Current sky state.
        /// @param sun_alt_deg Sun altitude in degrees.
        /// @return Magnitude penalty to subtract from the base limiting magnitude.
        [[nodiscard]] static f32 twilight_magnitude_penalty(SkyState state, f32 sun_alt_deg);
    };

} // namespace parallax::astro