#pragma once

/// @file measurement_technique.hpp
/// @brief MeasurementTechnique enum — observational methods used to measure object properties.

#include "core/types.hpp"

namespace parallax::knowledge
{

/// @brief Observational technique used to acquire a measurement.
///
/// Each technique maps to a specific instrument type.  Sci-fi techniques
/// are listed as placeholders for future sprints.
enum class MeasurementTechnique : u16
{
    None,                   ///< No technique / not measured
    BroadbandPhotometry,    ///< Standard BVRI flux measurement
    PrecisionPhotometry,    ///< High-cadence photometry (transits, variability)
    SpectroscopyLowRes,     ///< Low-resolution (R~500) spectroscopy
    SpectroscopyHighRes,    ///< High-resolution (R>10000) spectroscopy
    RadialVelocity,         ///< Doppler shift measurement
    Astrometry,             ///< Precise position and proper motion
    Interferometry,         ///< High angular resolution imaging
    PolarimetryLinear,      ///< Linear polarization measurement
    Coronagraphy,           ///< Starlight suppression for direct planet imaging
    RadioObservation,       ///< Radio-band observation
    XRayObservation,        ///< X-ray band observation

    // --- Sci-fi / future techniques (Sprint 16+) ---
    NeutrinoDetection,      ///< Neutrino flux from stellar core
    GravitationalWave,      ///< Gravitational wave strain measurement
    DirectNeuralImaging     ///< Far-future: direct neural interface imaging
};

} // namespace parallax::knowledge
