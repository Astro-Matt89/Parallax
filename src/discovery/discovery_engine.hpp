#pragma once
// discovery/discovery_engine.hpp - Detection threshold and discovery mechanics
//
// Models:
//  - Detection threshold (SNR-based)
//  - Confirmation observations
//  - Transit, parallax, and spectroscopic discovery methods

#include "universe/star.hpp"
#include "observatory/observer.hpp"
#include <vector>
#include <string>
#include <optional>
#include <functional>

namespace parallax {

// -----------------------------------------------------------------------
// DiscoveryType
// -----------------------------------------------------------------------
enum class DiscoveryType {
    DirectDetection,   ///< Direct imaging of a new object
    TransitMethod,     ///< Exoplanet via transit photometry
    ParallaxShift,     ///< Parallax measurement of nearby star
    Spectroscopic,     ///< Spectroscopic radial velocity detection
    Astrometric,       ///< Proper motion / astrometric anomaly
    PhotometricVariable, ///< Variable star detection
    Supernova,         ///< Supernova detection
    Comet,             ///< Comet discovery
};

inline const char* discoveryTypeName(DiscoveryType dt) {
    switch (dt) {
        case DiscoveryType::DirectDetection:    return "Direct Detection";
        case DiscoveryType::TransitMethod:      return "Transit Method";
        case DiscoveryType::ParallaxShift:      return "Parallax Shift";
        case DiscoveryType::Spectroscopic:      return "Spectroscopic";
        case DiscoveryType::Astrometric:        return "Astrometric";
        case DiscoveryType::PhotometricVariable:return "Photometric Variable";
        case DiscoveryType::Supernova:          return "Supernova";
        case DiscoveryType::Comet:              return "Comet";
        default:                                return "Unknown";
    }
}

// -----------------------------------------------------------------------
// Observation record
// -----------------------------------------------------------------------
struct Observation {
    double     jd{0.0};            ///< Julian Date of observation
    Equatorial target;             ///< Observed equatorial position
    double     snr{0.0};          ///< Achieved signal-to-noise ratio
    double     v_magnitude{0.0};  ///< Measured apparent magnitude
    double     exposure_s{0.0};   ///< Exposure time [s]
    bool       is_detection{false}; ///< Above threshold?
};

// -----------------------------------------------------------------------
// Discovery record
// -----------------------------------------------------------------------
struct Discovery {
    uint64_t       object_id{0};
    std::string    name;
    DiscoveryType  type{DiscoveryType::DirectDetection};
    double         jd_discovery{0.0};
    int            n_confirmations{0};
    std::vector<Observation> observations;
    bool           confirmed{false};
};

// -----------------------------------------------------------------------
// DiscoveryEngine
// -----------------------------------------------------------------------
class DiscoveryEngine {
public:
    /// Minimum SNR required for a detection claim.
    static constexpr double kDetectionSnrThreshold  = 5.0;
    /// Minimum SNR required for a discovery claim.
    static constexpr double kDiscoverySnrThreshold  = 7.0;
    /// Number of independent confirmations required.
    static constexpr int    kRequiredConfirmations   = 3;

    /// Minimum parallax shift detectable [mas] given the telescope diffraction
    /// limit and multiple epochs.
    static double parallaxDetectionLimit_mas(const Telescope& scope,
                                              int n_epochs = 6) {
        // Each epoch measures position to ~0.1 * diffraction_limit
        double precision_mas = scope.diffractionLimit_arcsec() * 100.0
                             / std::sqrt(static_cast<double>(n_epochs));
        return precision_mas;
    }

    /// Can we detect a parallax shift for this star?
    static bool canMeasureParallax(const Star& star,
                                   const Telescope& scope,
                                   int n_epochs = 6) {
        if (star.parallax_mas <= 0.0) return false;
        double limit = parallaxDetectionLimit_mas(scope, n_epochs);
        return star.parallax_mas > limit;
    }

    /// Transit depth for a planet of radius r_planet_re (Earth radii)
    /// transiting a star of radius r_star_rs (solar radii).
    static double transitDepth(double r_planet_re, double r_star_rs) {
        constexpr double kReRs = 0.00916;  // Earth radius / Solar radius
        double ratio = (r_planet_re * kReRs) / r_star_rs;
        return ratio * ratio;  // fractional flux drop
    }

    /// Minimum planet radius [Earth radii] detectable via transit
    /// given photometric precision (= 1/SNR) and stellar radius [solar radii].
    static double minimumDetectablePlanetRadius(double photometric_snr,
                                                 double star_radius_rs = 1.0) {
        if (photometric_snr <= 0.0) return 99.0;
        // Transit depth must exceed 1/SNR
        double min_depth = 1.0 / photometric_snr;
        constexpr double kReRs = 0.00916;
        return std::sqrt(min_depth) * star_radius_rs / kReRs;
    }

    // -----------------------------------------------------------------------
    // Core recording / confirmation workflow
    // -----------------------------------------------------------------------

    /// Record an observation and update discovery state.
    /// @returns true if this observation triggers a new confirmation.
    bool recordObservation(Discovery& disc, Observation obs) const;

    /// Check if a discovery is now confirmed.
    bool isConfirmed(const Discovery& disc) const {
        return disc.n_confirmations >= kRequiredConfirmations;
    }

    const std::vector<Discovery>& discoveries() const { return m_discoveries; }

    /// Register a new candidate discovery and return its index.
    std::size_t newDiscovery(uint64_t object_id, const std::string& name,
                              DiscoveryType type, double jd) {
        Discovery d;
        d.object_id   = object_id;
        d.name        = name;
        d.type        = type;
        d.jd_discovery= jd;
        m_discoveries.push_back(std::move(d));
        return m_discoveries.size() - 1;
    }

private:
    std::vector<Discovery> m_discoveries;
};

} // namespace parallax
