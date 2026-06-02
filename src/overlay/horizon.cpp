/// @file horizon.cpp
/// @brief Horizon line and cardinal marker overlay implementation.
///
/// The horizon line is a small circle at Alt = 0°, sampled at ~72 points
/// and projected via horizontal_to_screen(). Cardinal markers are drawn
/// as short tick lines + text labels at the 8 compass directions.

#include "overlay/horizon.hpp"

#include "core/logger.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <cmath>
#include <optional>
#include <vector>

namespace parallax::overlay
{

// =================================================================
// Public API
// =================================================================

void Horizon::update(const rendering::Camera& camera,
                     rendering::LineRenderer& lines,
                     ui::BitmapFont& font,
                     const ui::shell::ViewportRect& viewport)
{
    if (!m_visible)
    {
        return;
    }

    draw_horizon_line(camera, lines, viewport);
    draw_cardinal_markers(camera, lines, font, viewport);
}

void Horizon::set_visible(bool visible) { m_visible = visible; }
void Horizon::toggle_visible()
{
    m_visible = !m_visible;
    PLX_CORE_INFO("Horizon overlay: {}", m_visible ? "ON" : "OFF");
}
bool Horizon::is_visible() const { return m_visible; }

// =================================================================
// Horizon line
//
// A continuous line at Alt = 0° sweeping Az from 0 to 2π.
// Projected directly via horizontal_to_screen() — no RA/Dec involved.
// Points off-screen return nullopt → line break (same pattern as CoordGrid).
// =================================================================

void Horizon::draw_horizon_line(const rendering::Camera& camera,
                                rendering::LineRenderer& lines,
                                const ui::shell::ViewportRect& viewport)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const f64 aspect_ratio = static_cast<f64>(viewport.aspect());

    std::vector<std::optional<Vec2f>> points;
    points.reserve(kHorizonSamples);

    for (u32 s = 0; s < kHorizonSamples; ++s)
    {
        const f64 az_rad = static_cast<f64>(s) /
            static_cast<f64>(kHorizonSamples - 1) * astro_constants::kTwoPi;

        const astro::HorizontalCoord hz{.alt = 0.0, .az = az_rad};
        points.push_back(astro::Coordinates::horizontal_to_screen(
            hz, pointing, fov_rad, aspect_ratio));
    }

    // Submit consecutive visible pairs as line segments
    for (size_t i = 1; i < points.size(); ++i)
    {
        if (points[i - 1].has_value() && points[i].has_value())
        {
            lines.add_line(points[i - 1].value(), points[i].value(), kHorizonColor);
        }
    }
}

// =================================================================
// Cardinal markers
//
// At each of the 8 compass directions:
//   1. Project the horizon point (Alt=0°, Az=direction) to screen.
//   2. Project a point slightly above (Alt=kTickAltDeg) to screen.
//   3. Draw a tick line between the two.
//   4. Draw the label above the tick.
//
// North (Az=0°) gets a highlighted red color.
// =================================================================

void Horizon::draw_cardinal_markers(const rendering::Camera& camera,
                                    rendering::LineRenderer& lines,
                                    ui::BitmapFont& font,
                                    const ui::shell::ViewportRect& viewport)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const f64 aspect_ratio = static_cast<f64>(viewport.aspect());

    const f64 tick_alt_rad = kTickAltDeg * astro_constants::kDegToRad;

    for (u32 i = 0; i < kCardinalCount; ++i)
    {
        const f64 az_rad = kCardinalAzDeg[i] * astro_constants::kDegToRad;
        const bool is_north = (i == 0);

        // Horizon point
        const astro::HorizontalCoord hz_base{.alt = 0.0, .az = az_rad};
        const auto base_ndc = astro::Coordinates::horizontal_to_screen(
            hz_base, pointing, fov_rad, aspect_ratio);

        // Tick top — slightly above horizon
        const astro::HorizontalCoord hz_tick{.alt = tick_alt_rad, .az = az_rad};
        const auto tick_ndc = astro::Coordinates::horizontal_to_screen(
            hz_tick, pointing, fov_rad, aspect_ratio);

        // Draw tick line if both endpoints are on screen
        if (base_ndc.has_value() && tick_ndc.has_value())
        {
            const Vec4f tick_color = is_north ? kNorthTickColor : kCardinalTickColor;
            lines.add_line(base_ndc.value(), tick_ndc.value(), tick_color);
        }

        // Draw label above the tick (use tick position if available, else base)
        const auto& label_ndc = tick_ndc.has_value() ? tick_ndc : base_ndc;
        if (label_ndc.has_value())
        {
            const Vec2f px = ndc_to_pixel(label_ndc.value(), viewport);
            const Vec3f label_color = is_north ? kNorthLabelColor : kCardinalLabelColor;
            const char* label = kCardinalLabels[i];

            // Center the label horizontally on the tick mark
            const f32 label_w = static_cast<f32>(
                std::char_traits<char>::length(label)) * 8.0f * kLabelScale;

            // Place label above the tick (negative Y = upward in pixel space)
            font.draw_text(label,
                           px.x - label_w * 0.5f,
                           px.y - 20.0f,
                           kLabelScale, label_color);
        }
    }
}

// =================================================================
// NDC → pixel conversion (same formula as CoordGrid)
// =================================================================

Vec2f Horizon::ndc_to_pixel(Vec2f ndc, const ui::shell::ViewportRect& viewport)
{
    const f32 px = static_cast<f32>(viewport.x)
                 + (ndc.x + 1.0f) * 0.5f * static_cast<f32>(viewport.width);
    const f32 py = static_cast<f32>(viewport.y)
                 + (ndc.y + 1.0f) * 0.5f * static_cast<f32>(viewport.height);
    return {px, py};
}

} // namespace parallax::overlay