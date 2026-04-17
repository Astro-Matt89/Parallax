#pragma once

/// @file sky_state.hpp
/// @brief Sky condition calculator: twilight state, Moon contribution, limiting magnitude.
///
/// Determines observing conditions based on Sun altitude, Moon position/phase,
/// and site Bortle scale. Pure computation — no rendering, no state.
///
/// Twilight thresholds follow USNO definitions:
///   Day:                Sun alt > 0°
///   Civil twilight:     Sun alt -6° to 0°
///   Nautical twilight:  Sun alt -12° to -6°
///   Astro twilight:     Sun alt -18° to -12°
///   Night:              Sun alt < -18°
#include "astro/coordinates.hpp"
#include "astro/solar_system.hpp"
#include "core/types.hpp"

namespace parallax::astro
{
    /// @brief Sky state based on Sun altitude (USNO definitions).
    enum class SkyState : u8
    {
        Day,                ///< Sun altitude > 0°
        CivilTwilight,      ///< Sun altitude -6° to 0°
        NauticalTwilight,   ///< Sun altitude -12° to -6°
        AstroTwilight,      ///< Sun altitude -18° to -12°
        Night,              ///< Sun altitude < -18°
    };

    /// @brief Get a human-readable name for a SkyState value.
    [[nodiscard]] constexpr std::string_view sky_state_name(SkyState state)
    {
        switch (state)
        {
            case SkyState::Day:              return "DAY";
            case SkyState::CivilTwilight:    return "CIVIL TWI";
            case SkyState::NauticalTwilight: return "NAUTICAL TWI";
            case SkyState::AstroTwilight:    return "ASTRO TWI";
            case SkyState::Night:            return "NIGHT";
        }
        return "UNKNOWN";
    }

    /// @brief Complete sky conditions at a given instant and location.
    struct SkyConditions
    {
        SkyState state;                     ///< Twilight / day / night classification
        f32 sun_altitude_deg;               ///< Sun altitude above horizon (degrees)
        f32 moon_altitude_deg;              ///< Moon altitude above horizon (degrees)
        f32 moon_illumination;              ///< Moon illumination fraction (0..1)
        f32 moon_sky_brightness_mag;        ///< Additional sky brightness from Moon (mag/arcsec²)
        f32 effective_limiting_mag;         ///< Naked-eye limiting magnitude given all conditions
        f32 sky_brightness_zenith;          ///< Total zenith sky brightness (mag/arcsec²)
    };

    /// @brief Computes sky conditions from Sun/Moon states and observer location.
    ///
    /// Pure static computation class — no rendering, no mutable state.
    /// Feed it celestial body states and observer parameters, get back conditions.
    class SkyConditionCalculator
    {
    public:
        SkyConditionCalculator() = delete;

        /// @brief Compute sky conditions for the given instant.
        ///
        /// @param sun Sun state (needs equatorial coords for horizontal conversion).
        /// @param moon Moon state (needs equatorial coords + illumination).
        /// @param observer Observer geographic location.
        /// @param lst_rad Local sidereal time (radians).
        /// @param bortle_scale Bortle dark-sky scale (1–9).
        /// @return Complete sky conditions.
        [[nodiscard]] static SkyConditions compute(
            const CelestialBodyState& sun,
            const CelestialBodyState& moon,
            const ObserverLocation& observer,
            f64 lst_rad,
            f32 bortle_scale);

        /// @brief Classify sky state from Sun altitude alone.
        ///
        /// @param sun_alt_deg Sun altitude in degrees.
        /// @return Sky state classification.
        [[nodiscard]] static SkyState classify(f32 sun_alt_deg);

    private:
        /// @brief Compute Moon contribution to sky brightness.
        ///
        /// Simplified model (sprint 06 spec):
        ///   Full moon above horizon: ~2–3 mag/arcsec² brighter
        ///   Quarter moon: ~1–1.5 mag/arcsec²
        ///   Below horizon or new: no contribution
        ///
        /// @param moon_alt_deg Moon altitude in degrees.
        /// @param moon_illumination Moon illumination fraction (0..1).
        /// @return Sky brightness contribution in mag/arcsec² (positive = brighter sky = worse).
        [[nodiscard]] static f32 moon_brightness_contribution(
            f32 moon_alt_deg, f32 moon_illumination);

        /// @brief Compute twilight contribution to limiting magnitude reduction.
        ///
        /// @param sun_alt_deg Sun altitude in degrees.
        /// @return Magnitude reduction (positive value to subtract from limit).
        [[nodiscard]] static f32 twilight_magnitude_penalty(f32 sun_alt_deg);

        /// @brief Base zenith limiting magnitude from Bortle scale.
        ///
        /// Bortle 1 → 7.8 mag, Bortle 9 → 3.8 mag (linear interpolation).
        ///
        /// @param bortle_scale Bortle scale (1–9).
        /// @return Zenith naked-eye limiting magnitude.
        [[nodiscard]] static f32 bortle_limiting_magnitude(f32 bortle_scale);

        /// @brief Base zenith sky brightness from Bortle scale.
        ///
        /// Bortle 1 → 22.0 mag/arcsec², Bortle 9 → 17.0 mag/arcsec².
        ///
        /// @param bortle_scale Bortle scale (1–9).
        /// @return Zenith sky surface brightness (mag/arcsec²).
        [[nodiscard]] static f32 bortle_sky_brightness(f32 bortle_scale);
    };

} // namespace parallax::astro