#pragma once

/// @file mock_analyzer.hpp
/// @brief MockAnalyzer — Sprint 08 placeholder measurement extraction.

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_record.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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

// Intentionally coupled to MeasurementRecord so this mock stays aligned with
// the canonical value type defined by the knowledge module.
using MeasurementValue = decltype(knowledge::MeasurementRecord::value);

struct KnowledgeUpdate
{
    std::uint64_t                         object_id {0};
    std::string                           property_name;
    MeasurementValue                      value {0.0};
    double                                uncertainty {0.0};
    double                                snr {0.0};
    std::optional<knowledge::KnowledgeLevel> new_level {std::nullopt};
};

class MockAnalyzer
{
public:
    [[nodiscard]] std::vector<KnowledgeUpdate>
    analyze(const observation::DataRecord& record,
            const universe::Universe&      universe) const;
};

} // namespace parallax::analysis
