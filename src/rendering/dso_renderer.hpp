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
#include "ui/font.hpp"

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

namespace parallax::rendering
{
    /// @brief Renders deep sky object schematic icons and labels.
    ///
    /// Usage each frame:
    ///   1. Call update() with current camera, observer, LST, and the DSO catalog.
    ///   2. Icons + labels are submitted to the shared LineRenderer and BitmapFont.
    class DsoRenderer
    {
    public:
        DsoRenderer() = default;
        ~DsoRenderer() = default;

        DsoRenderer(const DsoRenderer&) = delete;
        DsoRenderer& operator=(const DsoRenderer&) = delete;
        DsoRenderer(DsoRenderer&&) = default;
        DsoRenderer& operator=(DsoRenderer&&) = default;

        /// @brief Project DSOs to screen and draw icons + labels.
        void update(const Camera& camera,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    std::span<const catalog::DsoEntry> catalog,
                    LineRenderer& lines,
                    ui::BitmapFont& font,
                    VkExtent2D viewport);

        void set_visible(bool visible);
        void toggle_visible();
        [[nodiscard]] bool is_visible() const;

        /// @brief Number of DSOs rendered in the last frame.
        [[nodiscard]] u32 get_rendered_count() const;

    private:
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

        /// @brief Icon color: magenta-pink (RGBA).
        static constexpr Vec4f kIconColor{0.8f, 0.4f, 0.6f, 0.7f};

        /// @brief Label color (RGB for BitmapFont).
        static constexpr Vec3f kLabelColor{0.8f, 0.4f, 0.6f};

        /// @brief Icon radius in NDC units (≈12px at 1080p).
        static constexpr f32 kIconRadiusNdc = 0.015f;

        /// @brief Label text scale.
        static constexpr f32 kLabelScale = 1.0f;
    };

} // namespace parallax::rendering