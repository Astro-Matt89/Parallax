#pragma once

/// @file property_descriptor.hpp
/// @brief PropertyDescriptor — metadata for a single observable property.

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_technique.hpp"

#include <string>

namespace parallax::knowledge
{

/// @brief Metadata for a single observable property of an astronomical object.
///
/// Describes which knowledge level the property unlocks at, what technique
/// is required to measure it, and the observing effort needed.
struct PropertyDescriptor
{
    std::string          name;                      ///< Unique property key (e.g. "orbital_period")
    KnowledgeLevel       unlocks_at;                ///< Level at which this property becomes accessible
    MeasurementTechnique required_technique;        ///< Technique needed to measure this property
    f32                  required_snr;              ///< Minimum SNR for confident detection
    f32                  required_observation_hours; ///< Typical session duration hint (hours)
};

} // namespace parallax::knowledge
