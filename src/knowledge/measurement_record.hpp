#pragma once

/// @file measurement_record.hpp
/// @brief MeasurementRecord — a single player-made measurement of an object property.

#include "core/types.hpp"

namespace parallax::knowledge
{

/// @brief A single measured value for one property of one object.
///
/// Stored inside ObjectKnowledge per property name.  Contains the measured
/// value, its uncertainty, the SNR of the observation, the session that
/// produced it, and the Julian Date of the measurement.
struct MeasurementRecord
{
    f64 value             {0.0};   ///< Measured value (units depend on property)
    f32 uncertainty       {0.0f};  ///< 1-sigma measurement uncertainty
    f32 snr               {0.0f};  ///< Signal-to-noise ratio of the observation
    u64 session_id        {0};     ///< ID of the ObservationSession that produced this
    f64 observation_jd    {0.0};   ///< Julian Date of the observation
};

} // namespace parallax::knowledge
