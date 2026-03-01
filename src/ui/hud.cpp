/// @file hud.cpp
/// @brief Retro green terminal HUD overlay implementation.

#include "ui/hud.hpp"

#include "astro/time_system.hpp"
#include "core/types.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace parallax::ui
{

// =================================================================
// Formatting helpers
// =================================================================

std::string format_ra(f64 ra_rad)
{
    // RA in radians → hours (0..24)
    f64 hours = ra_rad * astro_constants::kRadToHour;
    if (hours < 0.0)
    {
        hours += 24.0;
    }
    if (hours >= 24.0)
    {
        hours -= 24.0;
    }

    const int hh = static_cast<int>(hours);
    const f64 remainder_m = (hours - hh) * 60.0;
    const int mm = static_cast<int>(remainder_m);
    const int ss = static_cast<int>((remainder_m - mm) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02dh %02dm %02ds", hh, mm, ss);
    return buf;
}

std::string format_dms(f64 angle_rad)
{
    f64 deg = angle_rad * astro_constants::kRadToDeg;

    const char sign = (deg >= 0.0) ? '+' : '-';
    deg = std::abs(deg);

    const int dd = static_cast<int>(deg);
    const f64 remainder_m = (deg - dd) * 60.0;
    const int mm = static_cast<int>(remainder_m);
    const int ss = static_cast<int>((remainder_m - mm) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%02d %02d' %02d\"", sign, dd, mm, ss);
    return buf;
}

std::string format_az(f64 az_rad)
{
    f64 deg = az_rad * astro_constants::kRadToDeg;

    // Normalize to [0, 360)
    while (deg < 0.0)
    {
        deg += 360.0;
    }
    while (deg >= 360.0)
    {
        deg -= 360.0;
    }

    const int dd = static_cast<int>(deg);
    const f64 remainder_m = (deg - dd) * 60.0;
    const int mm = static_cast<int>(remainder_m);
    const int ss = static_cast<int>((remainder_m - mm) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%03d %02d' %02d\"", dd, mm, ss);
    return buf;
}

std::string format_time_scale(f64 scale)
{
    if (scale == 0.0)
    {
        return "PAUSED";
    }

    char buf[32];

    // Show integer if close to a whole number, otherwise 1 decimal
    if (std::abs(scale - std::round(scale)) < 0.01)
    {
        std::snprintf(buf, sizeof(buf), "x%d", static_cast<int>(scale));
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "x%.1f", scale);
    }

    return buf;
}

// =================================================================
// Construction
// =================================================================

Hud::Hud(const vulkan::Context& context,
         VkRenderPass render_pass,
         const std::filesystem::path& shader_dir)
    : m_font{context, render_pass, shader_dir}
{
}

// =================================================================
// update() — snapshot HUD data
// =================================================================

void Hud::update(const HudData& data)
{
    m_data = data;
}

// =================================================================
// render() — draw all panels
// =================================================================

void Hud::render(VkCommandBuffer cmd, VkExtent2D viewport_extent)
{
    if (!m_visible)
    {
        return;
    }

    const f32 vw = static_cast<f32>(viewport_extent.width);
    const f32 vh = static_cast<f32>(viewport_extent.height);

    draw_time_panel(vw, vh);
    draw_camera_panel(vw, vh);
    draw_observer_panel(vw, vh);
    draw_performance_panel(vw, vh);

    m_font.render(cmd, viewport_extent);
}

// =================================================================
// toggle / query
// =================================================================

void Hud::toggle_visible()
{
    m_visible = !m_visible;
}

bool Hud::is_visible() const
{
    return m_visible;
}

void Hud::toggle_time_format()                                          // ← Task 3.7
{
    auto next = static_cast<u8>(m_time_format) + 1;
    if (next >= static_cast<u8>(TimeDisplayFormat::kCount))
    {
        next = 0;
    }
    m_time_format = static_cast<TimeDisplayFormat>(next);
}

TimeDisplayFormat Hud::get_time_format() const                          // ← Task 3.7
{
    return m_time_format;
}

// =================================================================
// Top-left: title + time (respects m_time_format)                 ← Task 3.7
// =================================================================

void Hud::draw_time_panel(f32 /*vw*/, f32 /*vh*/)
{
    f32 x = kMargin;
    f32 y = kMargin;

    // Title
    m_font.draw_text("PARALLAX v0.1.0", x, y, kScale, kColorTitle);
    y += kLineSpacing;

    // Separator
    m_font.draw_text("------------------", x, y, kScale, kColorDim);
    y += kLineSpacing;

    // -----------------------------------------------------------------
    // Time display — show all three, highlight the active format         ← Task 3.7
    // -----------------------------------------------------------------

    // UTC date/time from Julian Date
    const auto dt = astro::TimeSystem::from_julian_date(m_data.julian_date);

    char utc_buf[32];
    std::snprintf(utc_buf, sizeof(utc_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  dt.year, dt.month, dt.day, dt.hour, dt.minute,
                  static_cast<int>(dt.second));

    const Vec3f utc_color = (m_time_format == TimeDisplayFormat::kUtc)
        ? kColorValue : kColorDim;
    m_font.draw_text("UTC  ", x, y, kScale, kColorLabel);
    m_font.draw_text(utc_buf, x + kGlyphW * 5, y, kScale, utc_color);
    y += kLineSpacing;

    // LST
    const std::string lst_str = format_ra(m_data.local_sidereal_time_rad);
    const Vec3f lst_color = (m_time_format == TimeDisplayFormat::kLst)
        ? kColorValue : kColorDim;
    m_font.draw_text("LST  ", x, y, kScale, kColorLabel);
    m_font.draw_text(lst_str, x + kGlyphW * 5, y, kScale, lst_color);
    y += kLineSpacing;

    // Julian Date
    char jd_buf[32];
    std::snprintf(jd_buf, sizeof(jd_buf), "%.3f", m_data.julian_date);

    const Vec3f jd_color = (m_time_format == TimeDisplayFormat::kJd)
        ? kColorValue : kColorDim;
    m_font.draw_text("JD   ", x, y, kScale, kColorLabel);
    m_font.draw_text(jd_buf, x + kGlyphW * 5, y, kScale, jd_color);
}

// =================================================================
// Top-right: camera pointing + FOV
// =================================================================

void Hud::draw_camera_panel(f32 vw, f32 /*vh*/)
{
    // Right-align: compute x from right edge
    // Longest line: "ALT  +45 12' 33\"" = 17 chars
    constexpr f32 kPanelChars = 18.0f;
    const f32 x_label = vw - kMargin - kPanelChars * kGlyphW;
    const f32 x_value = x_label + kGlyphW * 5;
    f32 y = kMargin;

    // ALT
    const f64 alt_rad = m_data.altitude_deg * astro_constants::kDegToRad;
    const std::string alt_str = format_dms(alt_rad);
    m_font.draw_text("ALT  ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(alt_str, x_value, y, kScale, kColorValue);
    y += kLineSpacing;

    // AZ
    const f64 az_rad = m_data.azimuth_deg * astro_constants::kDegToRad;
    const std::string az_str = format_az(az_rad);
    m_font.draw_text("AZ   ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(az_str, x_value, y, kScale, kColorValue);
    y += kLineSpacing;

    // FOV
    char fov_buf[16];
    std::snprintf(fov_buf, sizeof(fov_buf), "%.1f", m_data.fov_deg);

    m_font.draw_text("FOV  ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(fov_buf, x_value, y, kScale, kColorValue);
    y += kLineSpacing;

    // MLIM
    char mlim_buf[16];
    std::snprintf(mlim_buf, sizeof(mlim_buf), "%.1f", static_cast<double>(m_data.magnitude_limit));

    m_font.draw_text("MLIM ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(mlim_buf, x_value, y, kScale, kColorValue);
}

// =================================================================
// Bottom-left: observer location
// =================================================================

void Hud::draw_observer_panel(f32 /*vw*/, f32 vh)
{
    const f32 x = kMargin;
    f32 y = vh - kMargin - kLineSpacing * 3;

    // LAT
    const f64 lat_rad = m_data.latitude_deg * astro_constants::kDegToRad;
    const std::string lat_str = format_dms(lat_rad);
    const char* lat_dir = (m_data.latitude_deg >= 0.0) ? " N" : " S";

    m_font.draw_text("LAT  ", x, y, kScale, kColorLabel);
    m_font.draw_text(lat_str, x + kGlyphW * 5, y, kScale, kColorValue);
    m_font.draw_text(lat_dir, x + kGlyphW * 5 + kGlyphW * static_cast<f32>(lat_str.size()),
                     y, kScale, kColorValue);
    y += kLineSpacing;

    // LON
    const f64 lon_rad = m_data.longitude_deg * astro_constants::kDegToRad;
    const std::string lon_str = format_dms(lon_rad);
    const char* lon_dir = (m_data.longitude_deg >= 0.0) ? " E" : " W";

    m_font.draw_text("LON  ", x, y, kScale, kColorLabel);
    m_font.draw_text(lon_str, x + kGlyphW * 5, y, kScale, kColorValue);
    m_font.draw_text(lon_dir, x + kGlyphW * 5 + kGlyphW * static_cast<f32>(lon_str.size()),
                     y, kScale, kColorValue);
    y += kLineSpacing;

    // BORTLE
    char bortle_buf[8];
    std::snprintf(bortle_buf, sizeof(bortle_buf), "%d",
                  static_cast<int>(m_data.bortle_scale));

    m_font.draw_text("BORTLE ", x, y, kScale, kColorLabel);
    m_font.draw_text(bortle_buf, x + kGlyphW * 7, y, kScale, kColorValue);
}

// =================================================================
// Bottom-right: FPS, star count, time scale
// =================================================================

void Hud::draw_performance_panel(f32 vw, f32 vh)
{
    constexpr f32 kPanelChars = 22.0f;
    const f32 x_label = vw - kMargin - kPanelChars * kGlyphW;
    const f32 x_value = x_label + kGlyphW * 5;
    f32 y = vh - kMargin - kLineSpacing * 2;

    // FPS + star count on same line
    char fps_buf[32];
    std::snprintf(fps_buf, sizeof(fps_buf), "%-4d", static_cast<int>(m_data.fps));

    char star_buf[32];
    std::snprintf(star_buf, sizeof(star_buf), "* %u/%u",
                  m_data.visible_stars, m_data.total_stars);

    m_font.draw_text("FPS  ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(fps_buf, x_value, y, kScale, kColorValue);
    m_font.draw_text(star_buf, x_value + kGlyphW * 5, y, kScale, kColorDim);
    y += kLineSpacing;

    // TIME scale — highlight in bright green, or use warning color for paused/reverse
    const std::string ts = format_time_scale(m_data.time_scale);

    // Paused = dim, reverse = warning amber, forward = bright green              ← Task 3.7
    Vec3f time_color = kColorValue;
    if (m_data.time_scale == 0.0)
    {
        time_color = kColorDim;         // Paused → dim green
    }
    else if (m_data.time_scale < 0.0)
    {
        time_color = {1.0f, 0.5f, 0.0f};  // Reverse → amber warning
    }

    m_font.draw_text("TIME ", x_label, y, kScale, kColorLabel);
    m_font.draw_text(ts, x_value, y, kScale, time_color);
}

} // namespace parallax::ui