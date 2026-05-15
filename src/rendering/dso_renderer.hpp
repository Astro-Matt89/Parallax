#pragma once

/// @file dso_renderer.hpp
/// @brief Schematic icon renderer for deep sky objects (Messier catalog).
///
/// Draws type-specific icons using the LineRenderer:
///   Galaxy           → tilted ellipse
///   Nebula           → square
///   OpenCluster      → dashed circle
///   GlobularCluster  → circle + cross
///   SupernovaRemnant → circle
///   Other            → small diamond
///
/// Each visible DSO gets an icon + "M##" designation label.
/// Visibility controlled by camera magnitude limit (same as stars).
/// Toggle: D key.

#include "astro/coordinates.hpp"
#include "catalog/dso_entry.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "rendering/render_style.hpp"
#include "ui/font.hpp"
#include "universe/celestial_object.hpp"

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

namespace parallax::rendering
{
    /// @brief Renders deep sky object schematic icons and labels.
    ///
    /// Two usage paths:
    ///
    /// **Universe path (new):**
    ///   1. begin_frame(lines, font, viewport)
    ///   2. add_celestial_object(screen_pos, obj, style)  — one call per visible DSO
    ///
    /// **Legacy path (deprecated):**
    ///   1. update(camera, observer, lst_rad, catalog, lines, font, viewport)
    class DsoRenderer
    {
    public:
        DsoRenderer() = default;
        ~DsoRenderer() = default;

        DsoRenderer(const DsoRenderer&) = delete;
        DsoRenderer& operator=(const DsoRenderer&) = delete;
        DsoRenderer(DsoRenderer&&) = default;
        DsoRenderer& operator=(DsoRenderer&&) = default;

        // ---------------------------------------------------------------
        // Universe path (new API)
        // ---------------------------------------------------------------

        /// @brief Set up rendering context for this frame.
        ///
        /// Stores references to the shared line renderer, font, and viewport
        /// for use by subsequent add_celestial_object() calls.  Call once per
        /// frame before any add_celestial_object().
        void begin_frame(LineRenderer& lines, ui::BitmapFont& font, VkExtent2D viewport);

        /// @brief Draw a single DSO icon + label at the given pre-projected screen position.
        ///
        /// Reads type information from @c std::get_if<DsoData>(&obj.data).
        /// If the object has no DsoData, rendering is skipped (defensive).
        ///
        /// Visual styles:
        ///   Historical  — unchanged catalog colors (magenta-pink).
        ///   Confirmed   — cyan tint (player-discovered, verified ≥ 2 detections).
        ///   Candidate   — orange tint (player-detected once, not yet confirmed).
        ///
        /// @param screen_pos  Pre-projected NDC position (from Application).
        /// @param obj         CelestialObject with ObjectType::DeepSkyObject.
        /// @param style       RenderStyle — Historical, Confirmed, or Candidate.
        void add_celestial_object(Vec2f screen_pos,
                                  const universe::CelestialObject& obj,
                                  RenderStyle style = RenderStyle::Historical);

        // ---------------------------------------------------------------
        // Legacy path (deprecated)
        // ---------------------------------------------------------------

        /// @brief Project DSOs to screen and draw icons + labels.
        ///
        /// @deprecated Use begin_frame() + add_celestial_object() instead.
        [[deprecated("Use begin_frame() + add_celestial_object()")]]
        void update(const Camera& camera,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    std::span<const catalog::DsoEntry> catalog,
                    LineRenderer& lines,
                    ui::BitmapFont& font,
                    VkExtent2D viewport);

        // ---------------------------------------------------------------
        // Common
        // ---------------------------------------------------------------

        void set_visible(bool visible);
        void toggle_visible();
        [[nodiscard]] bool is_visible() const;

        /// @brief Number of DSOs rendered in the last frame.
        [[nodiscard]] u32 get_rendered_count() const;

    private:
        /// @brief Dispatch to draw_icon() and emit the label at the given screen position.
        void draw_dso_at(Vec2f screen_pos,
                         catalog::DsoType dso_type,
                         std::string_view label,
                         RenderStyle style) const;

        /// @brief Draw the type-appropriate icon at the given NDC position.
        static void draw_icon(catalog::DsoType type,
                              Vec2f center_ndc,
                              Vec4f color,
                              f32 radius_ndc,
                              LineRenderer& lines);

        /// @brief Draw a tilted ellipse (galaxy icon).
        static void draw_ellipse(Vec2f center, f32 rx, f32 ry,
                                 f32 tilt_rad, Vec4f color,
                                 LineRenderer& lines, u32 segments = 24);

        /// @brief Draw a square (nebula icon).
        static void draw_square(Vec2f center, f32 half_size,
                                Vec4f color, LineRenderer& lines);

        /// @brief Draw a dashed circle (open cluster icon).
        static void draw_dashed_circle(Vec2f center, f32 radius,
                                       Vec4f color, LineRenderer& lines,
                                       u32 segments = 16);

        /// @brief Draw a circle with cross (globular cluster icon).
        static void draw_circle_cross(Vec2f center, f32 radius,
                                      Vec4f color, LineRenderer& lines,
                                      u32 segments = 24);

        /// @brief Draw a plain circle (SNR / Other icon).
        static void draw_plain_circle(Vec2f center, f32 radius,
                                      Vec4f color, LineRenderer& lines,
                                      u32 segments = 24);

        /// @brief Convert NDC to pixel coordinates.
        [[nodiscard]] static Vec2f ndc_to_pixel(Vec2f ndc, VkExtent2D viewport);

        bool m_visible = true;
        u32 m_rendered_count = 0;

        /// @brief Transient per-frame rendering context (set by begin_frame).
        LineRenderer*  m_frame_lines    = nullptr;
        ui::BitmapFont* m_frame_font    = nullptr;
        VkExtent2D     m_frame_viewport = {};

        /// @brief Icon color: magenta-pink (RGBA) — Historical style.
        static constexpr Vec4f kIconColor{0.8f, 0.4f, 0.6f, 0.7f};

        /// @brief Icon color for Confirmed discoveries: cyan.
        static constexpr Vec4f kIconColorConfirmed{0.3f, 0.85f, 1.0f, 0.85f};

        /// @brief Icon color for Candidate detections: orange.
        static constexpr Vec4f kIconColorCandidate{1.0f, 0.55f, 0.1f, 0.7f};

        /// @brief Label color (RGB for BitmapFont) — Historical style.
        static constexpr Vec3f kLabelColor{0.8f, 0.4f, 0.6f};

        /// @brief Label color for Confirmed discoveries: cyan.
        static constexpr Vec3f kLabelColorConfirmed{0.3f, 0.85f, 1.0f};

        /// @brief Label color for Candidate detections: orange.
        static constexpr Vec3f kLabelColorCandidate{1.0f, 0.55f, 0.1f};

        /// @brief Icon radius in NDC units (≈12px at 1080p).
        static constexpr f32 kIconRadiusNdc = 0.015f;

        /// @brief Label text scale.
        static constexpr f32 kLabelScale = 1.0f;
    };

} // namespace parallax::rendering
