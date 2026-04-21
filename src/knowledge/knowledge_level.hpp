#pragma once

/// @file knowledge_level.hpp
/// @brief KnowledgeLevel enum — stratified knowledge tiers for every object.

#include "core/types.hpp"

namespace parallax::knowledge
{

/// @brief Stratified knowledge levels for astronomical objects.
///
/// Each level represents a tier of knowledge the player has unlocked
/// for a given object.  Real catalog objects start at Characterized (L3).
/// Procedural objects start at Unknown (L0) until discovered.
enum class KnowledgeLevel : u8
{
    Unknown       = 0,  ///< Object existence not known
    Detected      = 1,  ///< Detected as a source (position, magnitude)
    Classified    = 2,  ///< Basic type known (star type, galaxy morphology)
    Characterized = 3,  ///< Key properties measured (spectrum, distance)
    Detailed      = 4,  ///< Advanced properties (rotation, composition)
    Resolved      = 5,  ///< Structure resolved (sub-universe visible)
    FullyMapped   = 6,  ///< Maximum characterization
    Reserved      = 7   ///< Future use
};

/// @brief Baseline level assigned to all real catalog objects at game start.
constexpr KnowledgeLevel kHistoricalBaselineLevel = KnowledgeLevel::Characterized;

} // namespace parallax::knowledge
