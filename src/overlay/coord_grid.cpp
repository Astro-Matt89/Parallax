/// @file coord_grid.cpp
/// @brief Coordinate grid overlay implementation.
///
/// Grid lines are great/small circles on the celestial sphere, sampled at
/// ~72 points and projected to screen NDC via the SHARED coordinate pipeline.
///
/// Equatorial grid: uses project_radec_to_screen() for each sample.
/// Alt/Az grid: uses horizontal_to_screen() directly (no RA/Dec conversion).

#include "overlay/coord_grid.hpp"

#include "core/logger.hpp"

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace parallax::overlay
{

// =================================================================
// Public API
// =================================================================

void CoordGrid::update(const rendering::Camera& camera,
                       const astro::ObserverLocation& observer,
                       f64 lst_rad,
                       rendering::LineRenderer& lines,
                       ui::BitmapFont& font,
                       VkExtent2D viewport)
{
    if (m_type == GridType::None)
    {
        return;
    }

    if (m_type == GridType::Equatorial || m_type == GridType::Both)
    {
        draw_equatorial_grid(camera, observer, lst_rad, lines, font, viewport);
    }

    if (m_type == GridType::AltAzimuth || m_type == GridType::Both)
    {
        draw_altaz_grid(camera, lines, font, viewport);
    }
}

void CoordGrid::set_type(GridType type) { m_type = type; }
GridType CoordGrid::get_type() const { return m_type; }

void CoordGrid::cycle_type()
{
    switch (m_type)
    {
        case GridType::None:        m_type = GridType::Equatorial; break;
        case GridType::Equatorial:  m_type = GridType::AltAzimuth; break;
        case GridType::AltAzimuth:  m_type = GridType::Both;       break;
        case GridType::Both:        m_type = GridType::None;        break;
    }

    PLX_CORE_INFO("Coordinate grid: {}", get_type_name());
}

const char* CoordGrid::get_type_name() const
{
    switch (m_type)
    {
        case GridType::None:        return "OFF";
        case GridType::Equatorial:  return "EQ";
        case GridType::AltAzimuth:  return "ALTAZ";
        case GridType::Both:        return "BOTH";
    }
    return "???";
}

// =================================================================
// Equatorial grid
//
// RA lines: great circles at fixed RA, sweeping Dec from -90° to +90°.
//           These appear as curves because the equatorial grid rotates
//           with sidereal time relative to Alt/Az.
//
// Dec lines: small circles at fixed Dec, sweeping RA from 0 to 2π.
//            The celestial equator (Dec = 0°) is a great circle.
//
// All positions projected via the SHARED project_radec_to_screen().
// Points below the horizon or off-screen return nullopt → line break.
// =================================================================

void CoordGrid::draw_equatorial_grid(const rendering::Camera& camera,
                                     const astro::ObserverLocation& observer,
                                     f64 lst_rad,
                                     rendering::LineRenderer& lines,
                                     ui::BitmapFont& font,
                                     VkExtent2D viewport)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();

    // ---------------------------------------------------------------
    // RA lines: 24 lines at RA = 0h, 1h, …, 23h
    // Each is a great circle: sweep Dec from -90° to +90°
    // ---------------------------------------------------------------
    for (u32 h = 0; h < kRaLineCount; ++h)
    {
        const f64 ra_rad = static_cast<f64>(h) * astro_constants::kHourToRad;

        std::vector<std::optional<Vec2f>> points;
        points.reserve(kSamplesPerLine);

        for (u32 s = 0; s < kSamplesPerLine; ++s)
        {
            // Dec from -90° to +90° in kSamplesPerLine steps
            const f64 dec_rad = -astro_constants::kHalfPi +
                static_cast<f64>(s) / static_cast<f64>(kSamplesPerLine - 1) *
                astro_constants::kPi;

            points.push_back(astro::Coordinates::project_radec_to_screen(
                ra_rad, dec_rad, observer, lst_rad, pointing, fov_rad));
        }

        submit_curve(points, kEqColor, lines);
    }

    // ---------------------------------------------------------------
    // Dec lines: 17 lines at Dec = -80°, -70°, …, +80°
    // Each is a small circle: sweep RA from 0 to 2π
    // Dec = 0° (celestial equator) is rendered brighter
    // ---------------------------------------------------------------
    for (i32 d = -8; d <= 8; ++d)
    {
        const f64 dec_deg = static_cast<f64>(d) * 10.0;
        const f64 dec_rad = dec_deg * astro_constants::kDegToRad;
        const bool is_equator = (d == 0);

        std::vector<std::optional<Vec2f>> points;
        points.reserve(kSamplesPerLine);

        for (u32 s = 0; s < kSamplesPerLine; ++s)
        {
            const f64 ra_rad = static_cast<f64>(s) /
                static_cast<f64>(kSamplesPerLine - 1) * astro_constants::kTwoPi;

            points.push_back(astro::Coordinates::project_radec_to_screen(
                ra_rad, dec_rad, observer, lst_rad, pointing, fov_rad));
        }

        submit_curve(points, is_equator ? kEqBrightColor : kEqColor, lines);
    }

    // ---------------------------------------------------------------
    // Labels: RA labels at Dec = 0° crossing
    // ---------------------------------------------------------------
    for (u32 h = 0; h < kRaLineCount; ++h)
    {
        const f64 ra_rad = static_cast<f64>(h) * astro_constants::kHourToRad;
        const auto pos = astro::Coordinates::project_radec_to_screen(
            ra_rad, 0.0, observer, lst_rad, pointing, fov_rad);

        if (pos.has_value())
        {
            const Vec2f px = ndc_to_pixel(pos.value(), viewport);

            // Format: "0h", "1h", …, "23h"
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%uh", h);

            // Offset label slightly so it doesn't overlap the line intersection
            font.draw_text(buf, px.x + 2.0f, px.y - 18.0f,
                           kLabelScale, kEqLabelColor);
        }
    }

    // ---------------------------------------------------------------
    // Labels: Dec labels at RA = 0 crossing
    // ---------------------------------------------------------------
    for (i32 d = -8; d <= 8; ++d)
    {
        if (d == 0)
        {
            continue;  // Don't label "0°" — it's the equator, already obvious
        }

        const f64 dec_deg = static_cast<f64>(d) * 10.0;
        const f64 dec_rad = dec_deg * astro_constants::kDegToRad;
        const auto pos = astro::Coordinates::project_radec_to_screen(
            0.0, dec_rad, observer, lst_rad, pointing, fov_rad);

        if (pos.has_value())
        {
            const Vec2f px = ndc_to_pixel(pos.value(), viewport);

            // Format: "+80°", "-30°", etc.
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%+d%c",
                          static_cast<i32>(dec_deg), '\xF8');  // ° = 0xF8 in CP437

            font.draw_text(buf, px.x + 2.0f, px.y - 18.0f,
                           kLabelScale, kEqLabelColor);
        }
    }
}

// =================================================================
// Alt/Az grid
//
// Azimuth lines: great circles at fixed Az, sweeping Alt from 0° to 90°.
// Altitude lines: small circles at fixed Alt, sweeping Az from 0 to 2π.
//
// These project directly via horizontal_to_screen() — no RA/Dec involved.
// Points below horizon are not generated. Off-screen → nullopt → break.
// =================================================================

void CoordGrid::draw_altaz_grid(const rendering::Camera& camera,
                                rendering::LineRenderer& lines,
                                ui::BitmapFont& font,
                                VkExtent2D viewport)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();

    // ---------------------------------------------------------------
    // Azimuth lines: 24 lines at Az = 0°, 15°, 30°, …, 345°
    // Each sweeps Alt from 0° (horizon) to 90° (zenith)
    // ---------------------------------------------------------------
    for (u32 a = 0; a < kAzLineCount; ++a)
    {
        const f64 az_rad = static_cast<f64>(a) * 15.0 * astro_constants::kDegToRad;

        std::vector<std::optional<Vec2f>> points;
        points.reserve(kSamplesPerLine);

        for (u32 s = 0; s < kSamplesPerLine; ++s)
        {
            // Alt from 0° to 90° — only above horizon
            const f64 alt_rad = static_cast<f64>(s) /
                static_cast<f64>(kSamplesPerLine - 1) * astro_constants::kHalfPi;

            const astro::HorizontalCoord hz{.alt = alt_rad, .az = az_rad};
            points.push_back(astro::Coordinates::horizontal_to_screen(
                hz, pointing, fov_rad));
        }

        submit_curve(points, kAzColor, lines);
    }

    // ---------------------------------------------------------------
    // Altitude lines: 9 lines at Alt = 0°, 10°, 20°, …, 80°
    // Each sweeps Az from 0 to 2π
    // Alt = 0° (horizon) is rendered brighter
    // ---------------------------------------------------------------
    for (u32 a = 0; a < kAltLineCount; ++a)
    {
        const f64 alt_deg = static_cast<f64>(a) * 10.0;
        const f64 alt_rad = alt_deg * astro_constants::kDegToRad;
        const bool is_horizon = (a == 0);

        std::vector<std::optional<Vec2f>> points;
        points.reserve(kSamplesPerLine);

        for (u32 s = 0; s < kSamplesPerLine; ++s)
        {
            const f64 az_rad = static_cast<f64>(s) /
                static_cast<f64>(kSamplesPerLine - 1) * astro_constants::kTwoPi;

            const astro::HorizontalCoord hz{.alt = alt_rad, .az = az_rad};
            points.push_back(astro::Coordinates::horizontal_to_screen(
                hz, pointing, fov_rad));
        }

        submit_curve(points, is_horizon ? kAzBrightColor : kAzColor, lines);
    }

    // ---------------------------------------------------------------
    // Labels: azimuth degree values at horizon (Alt = 0°)
    // ---------------------------------------------------------------
    for (u32 a = 0; a < kAzLineCount; ++a)
    {
        const f64 az_deg = static_cast<f64>(a) * 15.0;
        const f64 az_rad = az_deg * astro_constants::kDegToRad;

        const astro::HorizontalCoord hz{.alt = 0.0, .az = az_rad};
        const auto pos = astro::Coordinates::horizontal_to_screen(
            hz, pointing, fov_rad);

        if (pos.has_value())
        {
            const Vec2f px = ndc_to_pixel(pos.value(), viewport);

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%u%c",
                          static_cast<u32>(az_deg), '\xF8');  // ° in CP437

            font.draw_text(buf, px.x + 2.0f, px.y - 18.0f,
                           kLabelScale, kAzLabelColor);
        }
    }

    // ---------------------------------------------------------------
    // Labels: altitude degree values at Az = 0° (north)
    // ---------------------------------------------------------------
    for (u32 a = 1; a < kAltLineCount; ++a)  // Skip Alt=0° (horizon labelled above)
    {
        const f64 alt_deg = static_cast<f64>(a) * 10.0;
        const f64 alt_rad = alt_deg * astro_constants::kDegToRad;

        const astro::HorizontalCoord hz{.alt = alt_rad, .az = 0.0};
        const auto pos = astro::Coordinates::horizontal_to_screen(
            hz, pointing, fov_rad);

        if (pos.has_value())
        {
            const Vec2f px = ndc_to_pixel(pos.value(), viewport);

            char buf[8];
            std::snprintf(buf, sizeof(buf), "%u%c",
                          static_cast<u32>(alt_deg), '\xF8');

            font.draw_text(buf, px.x + 2.0f, px.y - 18.0f,
                           kLabelScale, kAzLabelColor);
        }
    }
}

// =================================================================
// Utility: submit a sampled curve as line segments
//
// The points vector contains std::optional<Vec2f>.
// nullopt entries represent off-screen or below-horizon gaps.
// We emit a LINE segment only between consecutive visible points.
// =================================================================

void CoordGrid::submit_curve(const std::vector<std::optional<Vec2f>>& points,
                             Vec4f color,
                             rendering::LineRenderer& lines)
{
    for (size_t i = 1; i < points.size(); ++i)
    {
        if (points[i - 1].has_value() && points[i].has_value())
        {
            lines.add_line(points[i - 1].value(), points[i].value(), color);
        }
    }
}

// =================================================================
// NDC → pixel conversion
// =================================================================

Vec2f CoordGrid::ndc_to_pixel(Vec2f ndc, VkExtent2D viewport)
{
    // NDC [-1, 1] → pixel [0, width/height]
    // Vulkan NDC: -1 = top/left, +1 = bottom/right (Y already flipped
    // by horizontal_to_screen which negates proj_y for Vulkan)
    const f32 px = (ndc.x + 1.0f) * 0.5f * static_cast<f32>(viewport.width);
    const f32 py = (ndc.y + 1.0f) * 0.5f * static_cast<f32>(viewport.height);
    return {px, py};
}

} // namespace parallax::overlay