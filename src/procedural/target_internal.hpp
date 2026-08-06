#pragma once
/// @file target_internal.hpp
/// @brief Internal declarations shared between the procedural translation units.
///        NOT part of the public API — do not include from outside src/procedural/.

#include "target_families.hpp"
#include "interferometry/mulberry32.hpp"

#include <cstdint>
#include <vector>

namespace parallax::procedural
{
    using Rng = parallax::interferometry::Mulberry32;

    // ─── target_primitives.cpp ────────────────────────────────────────────────

    /// Render all components (emission + absorption) to a flat N×N sky grid.
    /// This is the C++ equivalent of renderTargetAt's inner loop (without the
    /// spectral evaluation that belongs to the caller).
    [[nodiscard]] std::vector<double> render_sky(
        const std::vector<Component>& components,
        double        lambda_m,
        std::uint32_t N);

    // ─── target_recipes.cpp ───────────────────────────────────────────────────

    /// Invoke the recipe for `family`, populating `m.components`, `m.subtype`,
    /// `m.temporal`, `m.physical`, `m.theta_obj`, `m.fov_mul` and metadata.
    /// Draw-order is BINDING — see SPECIFICA_10b §2 and per-recipe comments.
    void dispatch_recipe(TargetModel& m, Rng& rng, std::uint32_t N, Family family);

    /// Consume rarity roll + budget draw + capRoll + Fisher-Yates shuffle +
    /// modifier internal draws, exactly matching the JS `applyCompatibleModifiers`.
    void apply_compatible_modifiers(TargetModel& m, Rng& rng,
                                    Complexity complexity, std::uint32_t N);

    /// Italian family label — verbatim from sandbox FAMILY_LABEL.
    [[nodiscard]] std::string family_label(Family f);

    /// Italian subtype label — verbatim from sandbox SUBTYPE_LABEL.
    [[nodiscard]] std::string subtype_label(const std::string& subtype);

} // namespace parallax::procedural
