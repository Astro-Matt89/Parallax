#pragma once

/// @file render_style.hpp
/// @brief RenderStyle — visual style for celestial objects, driven by KnowledgeDatabase state.

#include "core/types.hpp"

namespace parallax::rendering
{

/// @brief Visual rendering style for a celestial object.
///
/// Historical objects (real catalog) always use Historical.
/// Procedural objects use Confirmed (≥ 2 independent detections) or
/// Candidate (1 detection, not yet confirmed).
enum class RenderStyle : u8
{
    Historical,   ///< Known from catalogs; unchanged normal rendering.
    Confirmed,    ///< Player-discovered, confirmed (≥ 2 independent detections).
    Candidate,    ///< Player-detected, not yet confirmed (1 detection).
};

} // namespace parallax::rendering
