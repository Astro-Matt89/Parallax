#pragma once

/// @file horizon.hpp
/// @brief Horizon line overlay with cardinal direction labels and tick marks.
///
/// Draws a continuous line at Alt = 0° across the full azimuth range,
/// sampled at ~72 points and projected to screen NDC via the shared
/// horizontal_to_screen() pipeline.
///
/// Cardinal markers at N (0°), NE (45°), E (90°), SE (135°),
/// S (180°), SW (225°), W (270°), NW (315°):
///   - Small tick mark above the horizon line
///   - Text label above the tick
///
/// Colors (per sprint_04.md):
///   - Horizon line:    warm brown  (0.5, 0.3, 0.2, 0.6)
///   - Cardinal labels: brighter    (0.7, 0.5, 0.3, 0.8)
///   - North label:     highlighted red (1.0, 0.3, 0.2, 0.8) — compass convention
///
/// Toggle: O key toggles visibility.

#include "astro/coordinates.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"

#include <vulkan/vulkan.h>

#include <optional>
#include <vector>

namespace parallax::ui::shell { struct ViewportRect; }

namespace parallax::overlay
{
    /// @brief Horizon line with N/NE/E/SE/S/SW/W/NW cardinal direction markers.
    ///
    /// Usage each frame:
    ///   1. Call update() with the current camera state.
    ///   2. Lines and labels are submitted to the shared LineRenderer and BitmapFont.
    class Horizon
    {
    public:
        Horizon() = default;
        ~Horizon() = default;

        Horizon(const Horizon&) = delete;
        Horizon& operator=(const Horizon&) = delete;
        Horizon(Horizon&&) = default;
        Horizon& operator=(Horizon&&) = default;

        /// @brief Generate horizon line and cardinal markers for the current frame.
        void update(const rendering::Camera& camera,
                    rendering::LineRenderer& lines,
                    ui::BitmapFont& font,
                    const ui::shell::ViewportRect& viewport);

        void set_visible(bool visible);
        void toggle_visible();
        [[nodiscard]] bool is_visible() const;

    private:
        /// @brief Draw the continuous horizon line at Alt = 0°.
        void draw_horizon_line(const rendering::Camera& camera,
                               rendering::LineRenderer& lines,
                               const ui::shell::ViewportRect& viewport);

        /// @brief Draw tick marks and labels at the 8 cardinal/intercardinal directions.
        void draw_cardinal_markers(const rendering::Camera& camera,
                                   rendering::LineRenderer& lines,
                                   ui::BitmapFont& font,
                                   const ui::shell::ViewportRect& viewport);

        /// @brief Convert NDC [-1,1] to screen pixel coordinates.
        [[nodiscard]] static Vec2f ndc_to_pixel(Vec2f ndc, const ui::shell::ViewportRect& viewport);

        bool m_visible = true;

        // --- Horizon line ---
        static constexpr u32 kHorizonSamples = 73;  ///< ~72 segments (every 5°)

        /// @brief Warm brown horizon line (RGBA).
        static constexpr Vec4f kHorizonColor{0.5f, 0.3f, 0.2f, 0.6f};

        // --- Cardinal markers ---

        /// @brief Number of cardinal/intercardinal directions.
        static constexpr u32 kCardinalCount = 8;

        /// @brief Azimuth (degrees) for each marker: N, NE, E, SE, S, SW, W, NW.
        static constexpr f64 kCardinalAzDeg[kCardinalCount] = {
            0.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 315.0
        };

        /// @brief Label strings for each marker.
        static constexpr const char* kCardinalLabels[kCardinalCount] = {
            "N", "NE", "E", "SE", "S", "SW", "W", "NW"
        };

        /// @brief Tick mark altitude in degrees (how far above horizon).
        static constexpr f64 kTickAltDeg = 1.5;

        /// @brief Cardinal label color — brighter warm (RGB for BitmapFont).
        static constexpr Vec3f kCardinalLabelColor{0.7f, 0.5f, 0.3f};

        /// @brief North label color — highlighted red (RGB for BitmapFont).
        static constexpr Vec3f kNorthLabelColor{1.0f, 0.3f, 0.2f};

        /// @brief Cardinal tick line color (RGBA).
        static constexpr Vec4f kCardinalTickColor{0.7f, 0.5f, 0.3f, 0.8f};

        /// @brief North tick line color (RGBA).
        static constexpr Vec4f kNorthTickColor{1.0f, 0.3f, 0.2f, 0.8f};

        /// @brief Label text scale.
        static constexpr f32 kLabelScale = 1.0f;
    };

} // namespace parallax::overlay