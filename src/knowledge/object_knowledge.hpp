#pragma once

/// @file object_knowledge.hpp
/// @brief ObjectKnowledge — what the player knows about a single celestial object.

#include "knowledge/knowledge_level.hpp"
#include "knowledge/measurement_record.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace parallax::knowledge
{

/// @brief All knowledge the player has accumulated about one astronomical object.
///
/// Keyed by object ID (same u64 encoding used by the Universe Engine).
/// Measurements are stored in a map keyed by property name, matching the
/// names defined in PropertyRegistry.
struct ObjectKnowledge
{
    std::uint64_t object_id            {0};                          ///< Universe Engine object ID
    KnowledgeLevel current_level       {KnowledgeLevel::Unknown};    ///< Highest tier reached
    std::unordered_map<std::string, MeasurementRecord> measurements; ///< Per-property records
    std::vector<std::uint64_t> session_ids;                          ///< Sessions that produced data
    std::uint32_t independent_detections {0};                        ///< Independent detection count
    bool is_confirmed                  {false};                      ///< True at >= 2 detections
    bool is_historical                 {false};                      ///< True for real catalog objects
};

} // namespace parallax::knowledge
