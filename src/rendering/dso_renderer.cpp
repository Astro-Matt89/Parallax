/// @file dso_renderer.cpp
/// @brief DSO schematic icon renderer implementation.
///
/// Each DSO is projected via the SHARED project_radec_to_screen() pipeline,
/// then drawn as a type-specific icon using the LineRenderer.

#include "rendering/dso_renderer.hpp"

#include "core/logger.hpp"

#include <cmath>
#include <format>

namespace parallax::rendering
{

// =================================================================
// Universe path — new API
// =================================================================

void DsoRenderer::begin_frame(LineRenderer& lines, ui::BitmapFont& font, VkExtent2D viewport)
{
    m_frame_lines    = &lines;
    m_frame_font     = &font;
    m_frame_viewport = viewport;
    m_rendered_count = 0;
}

void DsoRenderer::add_celestial_object(Vec2f screen_pos,
                                       const universe::CelestialObject& obj)
{
    if (!m_visible || !m_frame_lines || !m_frame_font)
    {
        return;
    }

    // Extract DsoData defensively via std::get_if.
    const auto* dso_data = std::get_if<universe::DsoData>(&obj.data);
    if (!dso_data)
    {
        return; // Object does not carry DsoData — skip rendering
    }

    // Build label: "M<num>" from source_id (Messier number).
    const std::string label = std::format("M{}", universe::decode_source_id(obj.id));

    draw_dso_at(screen_pos, dso_data->dso_type, label);

    ++m_rendered_count;
}

void DsoRenderer::draw_dso_at(Vec2f screen_pos,
                               catalog::DsoType dso_type,
                               std::string_view label) const
{
    draw_icon(dso_type, screen_pos, kIconColor, kIconRadiusNdc, *m_frame_lines);

    const Vec2f px = ndc_to_pixel(screen_pos, m_frame_viewport);
    const f32 offset_x = kIconRadiusNdc * static_cast<f32>(m_frame_viewport.width) * 0.5f + 4.0f;

    m_frame_font->draw_text(std::string{label},
                            px.x + offset_x,
                            px.y - 8.0f,
                            kLabelScale, kLabelColor);
}

// =================================================================
// Legacy path — deprecated
// =================================================================

void DsoRenderer::update(const Camera& camera,
                         const astro::ObserverLocation& observer,
                         f64 lst_rad,
                         std::span<const catalog::DsoEntry> catalog,
                         LineRenderer& lines,
                         ui::BitmapFont& font,
                         VkExtent2D viewport)
{
    m_rendered_count = 0;

    if (!m_visible || catalog.empty())
    {
        return;
    }

    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const f32 mag_limit = camera.get_magnitude_limit();

    for (const auto& dso : catalog)
    {
        // Magnitude filtering — same limit as stars
        if (dso.mag_v > mag_limit)
        {
            continue;
        }

        // Project RA/Dec → screen NDC via shared pipeline
        const auto pos = astro::Coordinates::project_radec_to_screen(
            dso.ra, dso.dec, observer, lst_rad, pointing, fov_rad);

        if (!pos.has_value())
        {
            continue;
        }

        // Draw type-specific icon
        draw_icon(dso.type, pos.value(), kIconColor, kIconRadiusNdc, lines);

        // Draw designation label offset to the right of the icon
        const Vec2f px = ndc_to_pixel(pos.value(), viewport);
        const f32 offset_x = kIconRadiusNdc * static_cast<f32>(viewport.width) * 0.5f + 4.0f;

        font.draw_text(dso.designation,
                       px.x + offset_x,
                       px.y - 8.0f,
                       kLabelScale, kLabelColor);

        ++m_rendered_count;
    }
}

void DsoRenderer::set_visible(bool visible) { m_visible = visible; }
void DsoRenderer::toggle_visible()
{
    m_visible = !m_visible;
    PLX_CORE_INFO("DSO overlay: {}", m_visible ? "ON" : "OFF");
}
bool DsoRenderer::is_visible() const { return m_visible; }
u32 DsoRenderer::get_rendered_count() const { return m_rendered_count; }

// =================================================================
// Icon dispatch
// =================================================================

void DsoRenderer::draw_icon(catalog::DsoType type,
                            Vec2f center_ndc,
                            Vec4f color,
                            f32 radius_ndc,
                            LineRenderer& lines)
{
    switch (type)
    {
        case catalog::DsoType::Galaxy:
            draw_ellipse(center_ndc, radius_ndc, radius_ndc * 0.5f,
                         0.5236f, color, lines);  // ~30° tilt
            break;

        case catalog::DsoType::Nebula:
            draw_square(center_ndc, radius_ndc, color, lines);
            break;

        case catalog::DsoType::OpenCluster:
            draw_dashed_circle(center_ndc, radius_ndc, color, lines);
            break;

        case catalog::DsoType::GlobularCluster:
            draw_circle_cross(center_ndc, radius_ndc, color, lines);
            break;

        case catalog::DsoType::SupernovaRemnant:
            draw_plain_circle(center_ndc, radius_ndc, color, lines);
            break;

        case catalog::DsoType::Other:
            // Diamond: a rotated square (45° tilt)
            draw_square(center_ndc, radius_ndc * 0.7f, color, lines);
            break;
    }
}

// =================================================================
// Galaxy: tilted ellipse
// =================================================================

void DsoRenderer::draw_ellipse(Vec2f center, f32 rx, f32 ry,
                               f32 tilt_rad, Vec4f color,
                               LineRenderer& lines, u32 segments)
{
    const f32 cos_t = std::cos(tilt_rad);
    const f32 sin_t = std::sin(tilt_rad);
    const f32 step = static_cast<f32>(astro_constants::kTwoPi) / static_cast<f32>(segments);

    Vec2f prev{};
    for (u32 i = 0; i <= segments; ++i)
    {
        const f32 angle = static_cast<f32>(i) * step;
        const f32 lx = rx * std::cos(angle);
        const f32 ly = ry * std::sin(angle);

        // Rotate by tilt
        const Vec2f cur{
            center.x + lx * cos_t - ly * sin_t,
            center.y + lx * sin_t + ly * cos_t
        };

        if (i > 0)
        {
            lines.add_line(prev, cur, color);
        }
        prev = cur;
    }
}

// =================================================================
// Nebula: square
// =================================================================

void DsoRenderer::draw_square(Vec2f center, f32 half_size,
                              Vec4f color, LineRenderer& lines)
{
    const Vec2f tl{center.x - half_size, center.y - half_size};
    const Vec2f tr{center.x + half_size, center.y - half_size};
    const Vec2f br{center.x + half_size, center.y + half_size};
    const Vec2f bl{center.x - half_size, center.y + half_size};

    lines.add_line(tl, tr, color);
    lines.add_line(tr, br, color);
    lines.add_line(br, bl, color);
    lines.add_line(bl, tl, color);
}

// =================================================================
// Open Cluster: dashed circle (every other segment drawn)
// =================================================================

void DsoRenderer::draw_dashed_circle(Vec2f center, f32 radius,
                                     Vec4f color, LineRenderer& lines,
                                     u32 segments)
{
    const f32 step = static_cast<f32>(astro_constants::kTwoPi) / static_cast<f32>(segments);

    for (u32 i = 0; i < segments; i += 2)  // Skip every other → dashed
    {
        const f32 a0 = static_cast<f32>(i) * step;
        const f32 a1 = static_cast<f32>(i + 1) * step;

        const Vec2f p0{center.x + radius * std::cos(a0),
                       center.y + radius * std::sin(a0)};
        const Vec2f p1{center.x + radius * std::cos(a1),
                       center.y + radius * std::sin(a1)};

        lines.add_line(p0, p1, color);
    }
}

// =================================================================
// Globular Cluster: circle + cross
// =================================================================

void DsoRenderer::draw_circle_cross(Vec2f center, f32 radius,
                                    Vec4f color, LineRenderer& lines,
                                    u32 segments)
{
    // Full circle
    draw_plain_circle(center, radius, color, lines, segments);

    // Horizontal bar
    lines.add_line({center.x - radius, center.y},
                   {center.x + radius, center.y}, color);

    // Vertical bar
    lines.add_line({center.x, center.y - radius},
                   {center.x, center.y + radius}, color);
}

// =================================================================
// SNR / Other: plain circle
// =================================================================

void DsoRenderer::draw_plain_circle(Vec2f center, f32 radius,
                                    Vec4f color, LineRenderer& lines,
                                    u32 segments)
{
    const f32 step = static_cast<f32>(astro_constants::kTwoPi) / static_cast<f32>(segments);

    Vec2f prev{};
    for (u32 i = 0; i <= segments; ++i)
    {
        const f32 angle = static_cast<f32>(i) * step;
        const Vec2f cur{center.x + radius * std::cos(angle),
                        center.y + radius * std::sin(angle)};

        if (i > 0)
        {
            lines.add_line(prev, cur, color);
        }
        prev = cur;
    }
}

// =================================================================
// NDC → pixel conversion
// =================================================================

Vec2f DsoRenderer::ndc_to_pixel(Vec2f ndc, VkExtent2D viewport)
{
    const f32 px = (ndc.x + 1.0f) * 0.5f * static_cast<f32>(viewport.width);
    const f32 py = (ndc.y + 1.0f) * 0.5f * static_cast<f32>(viewport.height);
    return {px, py};
}

} // namespace parallax::rendering