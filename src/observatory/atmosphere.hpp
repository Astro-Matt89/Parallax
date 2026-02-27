#pragma once
// observatory/atmosphere.hpp - Full atmospheric model for ground-based observing
//
// Models:
//  - Fried parameter (r₀) and seeing FWHM
//  - Rayleigh + Mie extinction
//  - Airmass
//  - Bortle scale / sky background
//  - Humidity, temperature, pressure effects

#include "core/math/coordinates.hpp"
#include <cmath>
#include <algorithm>

namespace parallax {

// -----------------------------------------------------------------------
// Bortle scale -> approximate sky background [mag/arcsec²]
// -----------------------------------------------------------------------
inline double bortleToSkyBackground(int bortle) {
    // Empirical fit (Schaefer 1998, Cinzano et al. 2001)
    static const double table[] = {
        0.0,   // index 0 unused
        22.0,  // 1 - pristine dark sky
        21.7,  // 2
        21.4,  // 3
        21.0,  // 4
        20.4,  // 5 - suburban/rural transition
        19.3,  // 6 - suburban
        18.5,  // 7
        17.5,  // 8 - city fringe
        16.5,  // 9 - inner city
    };
    bortle = std::clamp(bortle, 1, 9);
    return table[bortle];
}

// -----------------------------------------------------------------------
// AtmosphericConditions - snapshot of atmospheric state
// -----------------------------------------------------------------------
struct AtmosphericConditions {
    double seeing_arcsec{2.0};    ///< FWHM of stellar PSF due to turbulence
    double extinction_mag{0.20};  ///< Zenith extinction [magnitudes] (V-band)
    int    bortle{4};             ///< Bortle scale [1..9]
    double humidity_pct{40.0};    ///< Relative humidity [%]
    double temperature_c{15.0};   ///< Air temperature [°C]
    double pressure_hPa{1013.25}; ///< Atmospheric pressure [hPa]
    double wind_ms{3.0};          ///< Wind speed [m/s]
    double transparency{0.9};     ///< Broadband transparency fraction [0..1]

    /// Fried parameter r₀ at 500 nm [cm] derived from seeing.
    double friedParam_cm() const {
        // seeing ≈ 0.98 * λ / r₀  (λ in same units)
        // λ = 500 nm = 5e-7 m = 5e-5 cm
        // seeing FWHM [rad] = seeing_arcsec * 4.848e-6
        double fwhm_rad = seeing_arcsec * 4.848e-6;
        return (fwhm_rad > 0.0) ? (0.98 * 5e-7 / fwhm_rad * 100.0) : 20.0;
    }

    /// Sky background brightness accounting for Bortle scale [mag/arcsec²]
    double skyBackground() const {
        return bortleToSkyBackground(bortle);
    }
};

// -----------------------------------------------------------------------
// AtmosphericModel - computes extinction, refraction, and seeing
// -----------------------------------------------------------------------
class AtmosphericModel {
public:
    explicit AtmosphericModel(const AtmosphericConditions& cond = {})
        : m_cond(cond) {}

    const AtmosphericConditions& conditions() const { return m_cond; }
    AtmosphericConditions& conditions() { return m_cond; }

    // -----------------------------------------------------------------------
    // Extinction
    // -----------------------------------------------------------------------

    /// Total extinction in magnitudes at a given altitude [degrees].
    /// Combines Rayleigh, aerosol (Mie), and ozone contributions.
    double extinction_mag(double alt_deg) const {
        double X = airmass(alt_deg);
        return (m_cond.extinction_mag * m_cond.transparency) * X;
    }

    /// Apparent magnitude after atmospheric extinction.
    double apparentMagnitude(double true_mag, double alt_deg) const {
        return true_mag + extinction_mag(alt_deg);
    }

    // -----------------------------------------------------------------------
    // Seeing
    // -----------------------------------------------------------------------

    /// Effective seeing FWHM [arcsec] at altitude alt_deg (turbulence
    /// increases towards the horizon due to longer path through atmosphere).
    double effectiveSeeing_arcsec(double alt_deg) const {
        double X = airmass(alt_deg);
        // Seeing degrades as X^(3/5) for Kolmogorov turbulence
        return m_cond.seeing_arcsec * std::pow(X, 0.6);
    }

    // -----------------------------------------------------------------------
    // Refraction
    // -----------------------------------------------------------------------

    /// Atmospheric refraction correction [arcsec] at observed altitude.
    /// Uses the Saemundsson approximation.
    double refraction_arcsec(double apparent_alt_deg) const {
        if (apparent_alt_deg < 0.5) return 0.0;
        // Temperature and pressure correction factor
        double f = (m_cond.pressure_hPa / 1010.0)
                 * (283.0 / (273.0 + m_cond.temperature_c));
        // R is in arcminutes per Saemundsson (1986); multiply by 60 for arcsec
        double R = 1.02 / std::tan((apparent_alt_deg + 10.3 /
                                     (apparent_alt_deg + 5.11)) * kDegRad);
        double result = R * f * 60.0; // arcsec
        return std::max(result, 0.0);
    }

    // -----------------------------------------------------------------------
    // Sky background
    // -----------------------------------------------------------------------

    /// Effective sky background [mag/arcsec²] at a given altitude,
    /// accounting for airglow gradient.
    double skyBackground(double alt_deg) const {
        double base = m_cond.skyBackground();
        // Sky is ~0.5 mag brighter at horizon due to airglow path length
        double airglow = 0.5 * (1.0 - alt_deg / 90.0);
        return base - airglow;
    }

    // -----------------------------------------------------------------------
    // Airmass - delegate to free function
    // -----------------------------------------------------------------------
    static double airmass(double alt_deg) {
        return ::parallax::airmass(alt_deg);
    }

private:
    AtmosphericConditions m_cond;
};

} // namespace parallax
