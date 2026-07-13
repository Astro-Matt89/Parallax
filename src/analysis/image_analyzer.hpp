#pragma once

/// @file image_analyzer.hpp
/// @brief ImageAnalyzer — Sprint 10a real measurement extraction from multispectral images.

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_record.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace parallax::imaging
{
    class MultispectralImage;
}

namespace parallax::observation
{
    struct DataRecord;
}

namespace parallax::universe
{
    class Universe;
}

namespace parallax::analysis
{

/// @brief Alias for the canonical measurement value type (matches MeasurementRecord::value).
using MeasurementValue = decltype(knowledge::MeasurementRecord::value);

/// @brief A single knowledge update emitted by the analyzer and applied to the KnowledgeDatabase.
struct KnowledgeUpdate
{
    std::uint64_t                            object_id     {0};
    std::string                              property_name;
    MeasurementValue                         value         {0.0};
    double                                   uncertainty   {0.0};
    double                                   snr           {0.0};
    std::optional<knowledge::KnowledgeLevel> new_level     {std::nullopt};
};

/// @brief Real image analyzer: extracts photometric measurements from a
///        completed multispectral observation and emits KnowledgeUpdates.
///
/// Analysis steps (per completed observation):
///  1. Locate the target object in the universe.
///  2. Per-band noise estimation via the median + k*MAD estimator.
///  3. Peak detection: centre pixel must exceed noise_floor * kDetectionSigma.
///  4. Absolute photometry from the universe's ground-truth properties (with
///     SNR-derived uncertainty, analogous to the decommissioned MockAnalyzer).
///  5. Multispectral colour index from band-pixel flux ratios (image-derived;
///     does not require calibration against an absolute flux scale).
///  6. Knowledge-level gating: same SNR thresholds as the former MockAnalyzer
///     (L1 >= 5, L2 >= 20, L3 >= 50) so the gameplay progression is unchanged.
class ImageAnalyzer
{
public:
    /// @brief Analyse a completed observation and emit KnowledgeUpdates.
    ///
    /// Returns an empty vector if the target cannot be resolved or if no band
    /// of the image detects a source above the noise floor.
    ///
    /// @param record   DataRecord produced by the completed session (provides
    ///                 target_object_id and achieved_snr).
    /// @param image    MultispectralImage formed for the same session.
    /// @param universe Read-only universe for target property lookup.
    [[nodiscard]] std::vector<KnowledgeUpdate> analyze(
        const observation::DataRecord&     record,
        const imaging::MultispectralImage& image,
        const universe::Universe&          universe) const;
};

} // namespace parallax::analysis
