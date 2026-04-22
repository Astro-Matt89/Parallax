#pragma once

/// @file data_record.hpp
/// @brief DataRecord — a single measurement result produced by an observation session.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace parallax::observation
{

/// @brief Category of data contained in a DataRecord.
enum class DataType
{
    PhotometricMeasurement, ///< Single-epoch flux/magnitude measurement
    LightCurve,             ///< Time-series photometric data
    Spectrum,               ///< Spectral energy distribution or line profile
    Image,                  ///< Pixel-grid image of the target
    SurveySourceList,       ///< Catalogue of detected sources from a survey scan
    Mock,                   ///< Synthetic record used for testing
};

/// @brief All data produced by one observation, keyed by session and object.
///
/// Scalar measurements and their uncertainties are stored in named maps so
/// that the Knowledge System can ingest them without knowing the instrument
/// type.  The @c raw_data blob is reserved for future image/spectrum storage.
struct DataRecord
{
    std::uint64_t   id                {0};   ///< Unique record identifier
    std::uint64_t   session_id        {0};   ///< Session that produced this record
    std::uint64_t   target_object_id  {0};   ///< 0 for survey scans
    DataType        type              {DataType::PhotometricMeasurement};
    std::string     technique;               ///< e.g. "photometry", "spectroscopy", "mock"
    double          observation_jd    {0.0}; ///< Julian Date of the mid-point of the observation
    double          duration_hours    {0.0}; ///< Total integration time
    double          achieved_snr      {0.0}; ///< Signal-to-noise ratio at end of integration

    /// @brief Named scalar measurements (e.g. "mag_v" → 8.42).
    std::unordered_map<std::string, double> measurements;

    /// @brief Per-measurement 1-σ uncertainties matching @c measurements keys.
    std::unordered_map<std::string, float>  uncertainties;

    /// @brief Raw binary payload (empty for Mock records).
    std::vector<std::uint8_t> raw_data;
};

} // namespace parallax::observation
