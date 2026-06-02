#pragma once

/// @file constellations.hpp
/// @brief Constellation overlay — IAU stick figures connecting Hipparcos stars.
///
/// Loads constellation line/name data from CSV, resolves HIP IDs against
/// the loaded star catalog, and renders stick-figure lines + abbreviation
/// labels each frame through the LineRenderer and BitmapFont.

#include "astro/coordinates.hpp"
#include "catalog/star_entry.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward declare to avoid pulling universe headers into the overlay layer.
namespace parallax::universe { class Universe; }
namespace parallax::ui::shell { struct ViewportRect; }

namespace parallax::overlay
{
    /// @brief One constellation: abbreviation, full name, and HIP-pair line segments.
    struct ConstellationData
    {
        std::string abbreviation;                       ///< IAU 3-letter code ("Ori")
        std::string name;                               ///< Full name ("Orion")
        std::vector<std::pair<u32, u32>> segments;      ///< HIP ID pairs
    };

    /// @brief The 88 IAU constellation stick figures rendered as sky overlays.
    ///
    /// Lifecycle (Universe path — new):
    ///   1. load()                 — parse CSV files
    ///   2. resolve_via_universe() — store Universe pointer for HIP resolution
    ///   3. update()               — each frame, transform + submit geometry
    ///
    /// Lifecycle (legacy path — deprecated):
    ///   1. load()          — parse CSV files
    ///   2. resolve_stars() — build HIP → catalog index map, store catalog ref
    ///   3. update()        — each frame, transform + submit geometry
    class Constellations
    {
    public:
        Constellations() = default;
        ~Constellations() = default;

        Constellations(const Constellations&) = delete;
        Constellations& operator=(const Constellations&) = delete;
        Constellations(Constellations&&) = default;
        Constellations& operator=(Constellations&&) = default;

        /// @brief Load constellation line segments and names from CSV files.
        /// @param lines_path Path to constellation_lines.csv.
        /// @param names_path Path to constellation_names.csv.
        /// @return True if at least some data was loaded.
        [[nodiscard]] bool load(const std::filesystem::path& lines_path,
                                const std::filesystem::path& names_path);

        /// @brief Register the Universe facade for HIP ID resolution.
        ///
        /// After this call, update() resolves HIP IDs via Universe::resolve_hip()
        /// instead of the legacy catalog span.  The Universe pointer must remain
        /// valid for the lifetime of this object.
        ///
        /// Clears the legacy HIP-to-index map and catalog span.
        ///
        /// @param universe  Pointer to the Universe facade (may be nullptr to revert).
        void resolve_via_universe(const universe::Universe* universe);

        /// @brief Build the HIP ID → catalog vector index lookup table.
        ///
        /// @deprecated Use resolve_via_universe() instead.
        ///
        /// Must be called after the star catalog is loaded and before the first
        /// update(). Only stars present in the catalog can be resolved; missing
        /// HIP IDs are logged and the corresponding segments skipped at render time.
        ///
        /// @param catalog The loaded star catalog (lifetime must outlive this object).
        [[deprecated("Use resolve_via_universe()")]]
        void resolve_stars(std::span<const catalog::StarEntry> catalog);

        /// @brief Transform constellation stars and submit lines + labels for this frame.
        void update(const rendering::Camera& camera,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    rendering::LineRenderer& lines,
                    ui::BitmapFont& font,
                    const ui::shell::ViewportRect& viewport);

        void set_visible(bool visible);
        void toggle_visible();
        [[nodiscard]] bool is_visible() const;

        [[nodiscard]] u32 get_constellation_count() const;
        [[nodiscard]] u32 get_segment_count() const;

    private:
        [[nodiscard]] bool load_lines(const std::filesystem::path& path);
        void load_names(const std::filesystem::path& path);

        /// @brief Legacy HIP resolution — returns catalog index.
        [[nodiscard]] std::optional<u32> resolve_hip_legacy(u32 hip_id) const;

        /// @brief Universe-path HIP resolution — returns RA/Dec pair.
        [[nodiscard]] std::optional<std::pair<double, double>> resolve_hip_universe(u32 hip_id) const;

        std::vector<ConstellationData> m_constellations;
        bool m_visible = true;

        /// @brief HIP catalog_id → index into the star catalog vector (legacy path).
        std::unordered_map<u32, u32> m_hip_to_index;

        /// @brief Non-owning view of the star catalog (legacy path — set by resolve_stars).
        std::span<const catalog::StarEntry> m_catalog;

        /// @brief Non-owning pointer to the Universe facade (Universe path — set by resolve_via_universe).
        const universe::Universe* m_universe = nullptr;

        /// @brief Dim blue-gray overlay color (RGBA).
        static constexpr Vec4f kLineColor{0.3f, 0.4f, 0.6f, 0.5f};

        /// @brief Label color (same hue, slightly brighter).
        static constexpr Vec3f kLabelColor{0.3f, 0.4f, 0.6f};

        /// @brief Label text scale (native 8×16 font).
        static constexpr f32 kLabelScale = 1.0f;
    };

} // namespace parallax::overlay