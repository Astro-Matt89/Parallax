#pragma once

/// @file session_types.hpp
/// @brief Observation session enums and parameter/progress structs.

#include <cstdint>
#include <string>
#include <vector>

namespace parallax::observation
{

/// @brief High-level category of an observation session.
enum class SessionType
{
    PointedObservation, ///< Targeted observation of a specific object
    SurveyScan,         ///< Area scan of a sky region
};

/// @brief Lifecycle state of an observation session.
enum class SessionState
{
    Scheduled, ///< Session created but not yet started
    InProgress, ///< Currently accumulating data
    Completed,  ///< Finished successfully
    Aborted,    ///< Stopped before completion by the player
    Failed,     ///< Terminated due to an error condition
};

/// @brief Sky region used as the target for survey-type sessions.
struct SkyRegion
{
    double center_ra  {0.0}; ///< Right ascension of region centre (radians)
    double center_dec {0.0}; ///< Declination of region centre (radians)
    double radius_rad {0.0}; ///< Angular radius of the region (radians)
};

/// @brief Input parameters that define a planned observation session.
struct SessionParameters
{
    SessionType     type                    {SessionType::PointedObservation};
    std::uint64_t   target_object_id        {0};   ///< 0 when type == SurveyScan
    SkyRegion       target_region;                  ///< Used when type == SurveyScan
    std::uint64_t   instrument_id           {0};
    double          planned_duration_hours  {0.0};
    double          start_julian_date       {0.0};
    std::string     technique;                      ///< e.g. "photometry", "spectroscopy", "mock"
};

/// @brief Runtime progress snapshot for an active or completed session.
struct SessionProgress
{
    SessionState            state               {SessionState::Scheduled};
    double                  elapsed_hours       {0.0};
    double                  accumulated_snr     {0.0};
    double                  completion_fraction {0.0}; ///< Range 0.0–1.0
    std::vector<std::string> log;                       ///< Human-readable event log
};

} // namespace parallax::observation
