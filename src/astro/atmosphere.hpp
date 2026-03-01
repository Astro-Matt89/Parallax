#pragma once

/// @file atmosphere.hpp
/// @brief Atmospheric refraction, extinction, airmass, sky brightness, and limiting magnitude.
///
/// All methods are const and pure — no side effects, no GPU dependencies.
/// Formulas sourced from docs/architecture/atmosphere_model.md.

#include "core/types.hpp"

namespace parallax::astro
{
    /// @brief Atmospheric condition parameters.
    struct AtmosphereParams
    {
        f32 pressure_mbar = 1013.25f;    ///< Atmospheric pressure (mbar)
        f32 temperature_c = 15.0f;       ///< Temperature (°C)
        f32 extinction_coeff = 0.20f;    ///< Extinction coefficient k (mag/airmass, V band)
        f32 bortle_scale = 4.0f;         ///< Bortle dark-sky scale (1–9)
    };

    /// @brief Atmospheric model for refraction, extinction, and sky brightness.
    ///
    /// Implements the essential Phase 1 atmospheric effects:
    /// - Bennett refraction formula (apparent position shift)
    /// - Rozenberg airmass formula (atmospheric path length)
    /// - Extinction in magnitudes and as a linear brightness factor
    /// - Sky surface brightness at zenith from Bortle scale
    /// - Naked-eye limiting magnitude at a given altitude
    ///
    /// All methods are `const` and pure. Fully testable in isolation.
    class Atmosphere
    {
    public:
        /// @brief Construct with given atmospheric conditions.
        /// @param params Atmospheric parameters (pressure, temperature, extinction, Bortle).
        explicit Atmosphere(const AtmosphereParams& params = {});

        /// @brief Atmospheric refraction correction.
        ///
        /// Returns the angular shift (in radians) to add to the true altitude
        /// to obtain the apparent (refracted) altitude.
        ///
        /// Uses the Bennett formula:
        ///   R = 1 / tan(h + 7.31 / (h + 4.4))  [arcminutes, h in degrees]
        ///   R_corrected = R × (P / 1010) × (283 / (273 + T))
        ///
        /// For h < 0°: clamps to the horizon value.
        ///
        /// @param true_altitude_rad True altitude in radians.
        /// @return Refraction correction in radians (always ≥ 0).
        [[nodiscard]] f64 refraction(f64 true_altitude_rad) const;

        /// @brief Airmass at a given true altitude.
        ///
        /// Uses the Rozenberg (1966) formula:
        ///   X = 1 / (cos(z) + 0.025 × exp(-11 × cos(z)))
        ///   where z = π/2 - altitude (zenith angle)
        ///
        /// For alt < 0°: returns a large value (40.0).
        ///
        /// @param true_altitude_rad True altitude in radians.
        /// @return Airmass (dimensionless, ≥ 1.0 at zenith).
        [[nodiscard]] f64 airmass(f64 true_altitude_rad) const;

        /// @brief Extinction in magnitudes at a given altitude.
        ///
        /// Δm = k × X, where k is the extinction coefficient and X is airmass.
        ///
        /// @param true_altitude_rad True altitude in radians.
        /// @return Extinction in magnitudes (always ≥ 0).
        [[nodiscard]] f32 extinction_mag(f64 true_altitude_rad) const;

        /// @brief Extinction as a linear brightness factor at a given altitude.
        ///
        /// factor = 10^(-0.4 × Δm), where Δm = k × X.
        /// A factor of 1.0 means no dimming; 0.0 means fully extinguished.
        ///
        /// @param true_altitude_rad True altitude in radians.
        /// @return Linear brightness factor in [0, 1].
        [[nodiscard]] f32 extinction_factor(f64 true_altitude_rad) const;

        /// @brief Sky surface brightness at zenith (mag/arcsec²).
        ///
        /// Derived from the Bortle scale using a linear interpolation
        /// of the midpoint values from the Bortle table:
        ///   Bortle 1 → 22.0 mag/arcsec²
        ///   Bortle 9 → 17.0 mag/arcsec²
        ///
        /// @return Zenith sky surface brightness in mag/arcsec².
        [[nodiscard]] f32 sky_brightness_zenith() const;

        /// @brief Naked-eye limiting magnitude at a given altitude.
        ///
        /// The faintest star visible to the naked eye, accounting for:
        /// - Sky brightness (from Bortle scale)
        /// - Atmospheric extinction at the given altitude
        ///
        /// Model:
        ///   m_lim = m_lim_zenith - k × (X - 1)
        ///
        /// Where m_lim_zenith is the zenith naked-eye limit from Bortle scale.
        ///
        /// @param true_altitude_rad True altitude in radians.
        /// @return Limiting magnitude (fainter = larger number).
        [[nodiscard]] f32 limiting_magnitude(f64 true_altitude_rad) const;

        /// @brief Update atmospheric parameters.
        /// @param params New atmospheric conditions.
        void set_params(const AtmosphereParams& params);

        /// @brief Get current atmospheric parameters.
        /// @return Const reference to the current parameters.
        [[nodiscard]] const AtmosphereParams& get_params() const;

    private:
        AtmosphereParams m_params;
    };

} // namespace parallax::astro