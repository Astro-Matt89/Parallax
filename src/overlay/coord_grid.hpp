#pragma once

/// @file coord_grid.hpp
/// @brief Equatorial (RA/Dec) and Horizontal (Alt/Az) coordinate grid overlays.
///
/// Draws sampled line strips on the celestial sphere projected to screen space.
/// Each grid line is a great circle or small circle, approximated by ~72 sample
/// points (every 5°) for smooth curves at typical FOVs.
///
/// Equatorial grid:
///   - 24 RA lines (every 1 h = 15°), Dec = -90° … +90°
///   - 17 Dec lines (every 10°, -80° … +80°), RA = 0 … 2π
///   - Celestial equator (Dec = 0°) rendered brighter
///   - Labels: "0h"–"23h" at Dec = 0° crossing; "+80°"–"-80°" at RA = 0 crossing
///   - Color: dim red-orange (0.6, 0.3, 0.2, 0.3)
///
/// Alt/Az grid:
///   - 24 azimuth lines (every 15°), Alt = 0° … 90°
///   - 9 altitude lines (every 10°, 0° … 80°), Az = 0 … 2π
///   - Horizon line (Alt = 0°) rendered brighter
///   - Labels: degree values at horizon
///   - Color: dim green (0.2, 0.5, 0.2, 0.3)
///
/// Toggle: G key cycles None → Equatorial → AltAzimuth → Both → None.

#include "astro/coordinates.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace parallax::ui::shell { struct ViewportRect; }

namespace parallax::overlay
{
    /// @brief Grid display mode.
    enum class GridType
    {
        None,
        Equatorial,     ///< RA/Dec lines
        AltAzimuth,     ///< Alt/Az lines
        Both
    };

    /// @brief Equatorial and Alt/Az coordinate grid overlay.
    ///
    /// Usage each frame:
    ///   1. Call update() with current camera, observer, and LST.
    ///   2. Lines and labels are submitted to the shared LineRenderer and BitmapFont.
    class CoordGrid
    {
    public:
        CoordGrid() = default;
        ~CoordGrid() = default;

        CoordGrid(const CoordGrid&) = delete;
        CoordGrid& operator=(const CoordGrid&) = delete;
        CoordGrid(CoordGrid&&) = default;
        CoordGrid& operator=(CoordGrid&&) = default;

        /// @brief Generate grid lines and labels for the current frame.
        void update(const rendering::Camera& camera,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    rendering::LineRenderer& lines,
                    ui::BitmapFont& font,
                    const ui::shell::ViewportRect& viewport);

        void set_type(GridType type);
        [[nodiscard]] GridType get_type() const;

        /// @brief Cycle: None → Equatorial → AltAzimuth → Both → None.
        void cycle_type();

        /// @brief Human-readable name of the current grid mode (for HUD).
        [[nodiscard]] const char* get_type_name() const;

    private:
        /// @brief Draw the equatorial (RA/Dec) grid.
        void draw_equatorial_grid(const rendering::Camera& camera,
                                  const astro::ObserverLocation& observer,
                                  f64 lst_rad,
                                  rendering::LineRenderer& lines,
                                  ui::BitmapFont& font,
                                  const ui::shell::ViewportRect& viewport);

        /// @brief Draw the horizontal (Alt/Az) grid.
        void draw_altaz_grid(const rendering::Camera& camera,
                             rendering::LineRenderer& lines,
                             ui::BitmapFont& font,
                             const ui::shell::ViewportRect& viewport);

        /// @brief Submit a sampled curve as line strip segments, skipping gaps.
        ///
        /// Points that project off-screen are marked as breaks in the strip.
        /// Consecutive visible points become individual line segments.
        static void submit_curve(const std::vector<std::optional<Vec2f>>& points,
                                 Vec4f color,
                                 rendering::LineRenderer& lines);

        /// @brief Convert NDC [-1,1] to screen pixel coordinates.
        [[nodiscard]] static Vec2f ndc_to_pixel(Vec2f ndc, const ui::shell::ViewportRect& viewport);

        GridType m_type = GridType::None;

        // --- Equatorial grid constants ---
        static constexpr u32 kRaLineCount  = 24;   ///< One line per hour
        static constexpr u32 kDecLineCount = 17;    ///< -80° to +80° in 10° steps
        static constexpr u32 kSamplesPerLine = 73;  ///< ~72 segments (every 5°)

        /// @brief Standard equatorial grid color (dim red-orange, low alpha).
        static constexpr Vec4f kEqColor{0.6f, 0.3f, 0.2f, 0.3f};

        /// @brief Brighter equatorial color for celestial equator (Dec = 0°).
        static constexpr Vec4f kEqBrightColor{0.7f, 0.4f, 0.25f, 0.5f};

        /// @brief Equatorial label color (RGB only, for BitmapFont).
        static constexpr Vec3f kEqLabelColor{0.7f, 0.4f, 0.25f};

        // --- Alt/Az grid constants ---
        static constexpr u32 kAzLineCount  = 24;    ///< Every 15°
        static constexpr u32 kAltLineCount = 9;      ///< 0° to 80° in 10° steps

        /// @brief Standard alt/az grid color (dim green, low alpha).
        static constexpr Vec4f kAzColor{0.2f, 0.5f, 0.2f, 0.3f};

        /// @brief Brighter alt/az color for horizon line (Alt = 0°).
        static constexpr Vec4f kAzBrightColor{0.3f, 0.6f, 0.3f, 0.5f};

        /// @brief Alt/Az label color (RGB only, for BitmapFont).
        static constexpr Vec3f kAzLabelColor{0.3f, 0.6f, 0.3f};

        /// @brief Label text scale.
        static constexpr f32 kLabelScale = 1.0f;
    };

} // namespace parallax::overlay