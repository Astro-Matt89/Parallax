/// @file side_panel.cpp
/// @brief Left side panel implementation — location presets, Bortle, magnitude, time speed.
///
/// SPRINT 05 Task 5.4

#include "ui/side_panel.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace parallax::ui
{

// =================================================================
// Built-in observer presets (verified astronomical data)
// =================================================================

static const std::vector<ObserverPreset> kBuiltinPresets =
{
    {"La Palma",    28.7606,  -17.8920, 2396.0, 1.0f},  // Roque de los Muchachos
    {"Mauna Kea",   19.8207, -155.4681, 4205.0, 1.0f},  // Mauna Kea Observatory
    {"Paranal",    -24.6272,  -70.4042, 2635.0, 1.0f},  // ESO Paranal
    {"McDonald",    30.6716, -104.0217, 2070.0, 2.0f},  // McDonald Observatory
    {"Greenwich",   51.4769,    0.0005,    0.0, 8.0f},  // Royal Observatory (urban)
};

// =================================================================
// Time speed presets
// =================================================================

static const std::vector<std::pair<std::string, f64>> kSpeedPresets =
{
    {"x1",    1.0},
    {"x10",   10.0},
    {"x100",  100.0},
    {"x1k",   1000.0},
    {"x10k",  10000.0},
};

// =================================================================
// Initialization
// =================================================================

void SidePanel::init(const SidePanelCallbacks& callbacks)
{
    m_presets = kBuiltinPresets;

    create_location_group(callbacks);
    create_bortle_group(callbacks);
    create_sky_group(callbacks);
    create_time_speed_group(callbacks);

    m_initialized = true;
    PLX_CORE_INFO("SidePanel initialized ({} presets, 9 Bortle buttons, "
                  "1 mag slider, {} speed buttons)",
                  m_presets.size(), kSpeedPresets.size());
}

// =================================================================
// Widget creation — Location presets
// =================================================================

void SidePanel::create_location_group(const SidePanelCallbacks& callbacks)
{
    const Vec2f dummy = {0.0f, 0.0f};

    m_preset_buttons.clear();
    m_preset_buttons.reserve(m_presets.size());

    for (std::size_t i = 0; i < m_presets.size(); ++i)
    {
        const auto& preset = m_presets[i];
        auto idx = static_cast<i32>(i);

        auto btn = std::make_unique<ToggleButton>(
            preset.name, dummy, Vec2f{kPresetBtnW, kPresetBtnH},
            [this, idx, cb = callbacks.set_location]()
            {
                m_selected_preset = idx;
                if (cb)
                {
                    const auto& p = m_presets[static_cast<std::size_t>(idx)];
                    cb(p.latitude_deg,
                       p.longitude_deg,
                       p.elevation_m,
                       p.default_bortle);
                }
            });

        m_preset_buttons.push_back(std::move(btn));
    }

    m_selected_preset = 0;
}

// =================================================================
// Widget creation — Bortle scale (1–9)
// =================================================================

void SidePanel::create_bortle_group(const SidePanelCallbacks& callbacks)
{
    const Vec2f dummy = {0.0f, 0.0f};
    const Vec2f size = {kSmallBtnW, kSmallBtnH};

    m_bortle_buttons.clear();
    m_bortle_buttons.reserve(9);

    for (i32 b = 1; b <= 9; ++b)
    {
        auto btn = std::make_unique<ToggleButton>(
            std::to_string(b), dummy, size,
            [this, b, cb = callbacks.set_bortle]()
            {
                m_selected_bortle = b - 1;
                if (cb)
                {
                    cb(static_cast<f32>(b));
                }
            });

        m_bortle_buttons.push_back(std::move(btn));
    }

    m_selected_bortle = 3;  // Bortle 4 default
}

// =================================================================
// Widget creation — Magnitude limit slider
// =================================================================

void SidePanel::create_sky_group(const SidePanelCallbacks& callbacks)
{
    const Vec2f dummy = {0.0f, 0.0f};

    m_slider_mag_limit = std::make_unique<Slider>(
        "MLIM", dummy, 200.0f,
        1.0f, 14.0f, 0.5f);
    m_slider_mag_limit->set_value(6.5f);
    m_slider_mag_limit->set_format_string("{:.1f}");

    if (callbacks.set_magnitude_limit)
    {
        m_slider_mag_limit->on_value_changed = [cb = callbacks.set_magnitude_limit](f32 val)
        {
            cb(val);
        };
    }
}

// =================================================================
// Widget creation — Time speed buttons
// =================================================================

void SidePanel::create_time_speed_group(const SidePanelCallbacks& callbacks)
{
    const Vec2f dummy = {0.0f, 0.0f};
    const Vec2f size = {44.0f, kSmallBtnH};

    m_speed_buttons.clear();
    m_speed_buttons.reserve(kSpeedPresets.size());

    for (const auto& [label, scale] : kSpeedPresets)
    {
        auto btn = std::make_unique<Button>(
            label, dummy, size,
            [scale, cb = callbacks.set_time_scale]()
            {
                if (cb)
                {
                    cb(scale);
                }
            });

        m_speed_buttons.push_back(std::move(btn));
    }
}

// =================================================================
// Layout — position all widgets based on viewport and slide state
// =================================================================

void SidePanel::layout_widgets(u32 viewport_width, u32 viewport_height)
{
    m_viewport_w = viewport_width;
    m_viewport_h = viewport_height;

    const f32 vh = static_cast<f32>(viewport_height);

    // Animated X position: slides in from the left
    const f32 hidden_x = -kPanelWidth;
    const f32 visible_x = 0.0f;
    m_panel_x = hidden_x + (visible_x - hidden_x) * m_slide_t;

    // Vertically centered
    m_panel_y = (vh - kPanelHeight) * 0.5f;
    if (m_panel_y < 4.0f)
    {
        m_panel_y = 4.0f;
    }

    // Content origin
    const f32 cx = m_panel_x + kPadding;
    f32 cy = m_panel_y + kPadding;

    // OBSERVER header — just text, no widget
    cy += kRowHeight;  // "=== OBSERVER ==="

    // Location label
    cy += kRowHeight;  // "Location"

    // Preset buttons (one per row)
    for (auto& btn : m_preset_buttons)
    {
        btn->set_position({cx, cy});
        cy += kPresetBtnH + 2.0f;
    }

    cy += 4.0f;

    // Lat/Lon/Elevation readouts — just text
    cy += kRowHeight;  // Latitude
    cy += kRowHeight;  // Longitude
    cy += kRowHeight;  // Elevation

    cy += 6.0f;

    // TIME section header
    cy += kRowHeight;  // "=== TIME ==="

    // UTC, LST, JD — just text
    cy += kRowHeight;  // UTC
    cy += kRowHeight;  // LST
    cy += kRowHeight;  // JD

    cy += 4.0f;

    // Speed label + buttons
    cy += kRowHeight;  // "Speed"

    // Row 1: first 3 speed buttons
    f32 sx = cx;
    for (std::size_t i = 0; i < m_speed_buttons.size() && i < 3; ++i)
    {
        m_speed_buttons[i]->set_position({sx, cy});
        sx += 44.0f + kSmallBtnSpacing;
    }
    cy += kSmallBtnH + kSmallBtnSpacing;

    // Row 2: remaining speed buttons
    sx = cx;
    for (std::size_t i = 3; i < m_speed_buttons.size(); ++i)
    {
        m_speed_buttons[i]->set_position({sx, cy});
        sx += 44.0f + kSmallBtnSpacing;
    }
    cy += kSmallBtnH + 6.0f;

    // SKY section header
    cy += kRowHeight;  // "=== SKY ==="

    // Bortle label
    cy += kRowHeight;  // "Bortle"

    // Bortle buttons: row of 5
    sx = cx;
    for (i32 i = 0; i < 5 && i < static_cast<i32>(m_bortle_buttons.size()); ++i)
    {
        m_bortle_buttons[static_cast<std::size_t>(i)]->set_position({sx, cy});
        sx += kSmallBtnW + kSmallBtnSpacing;
    }
    cy += kSmallBtnH + kSmallBtnSpacing;

    // Bortle buttons: row of 4
    sx = cx;
    for (i32 i = 5; i < 9 && i < static_cast<i32>(m_bortle_buttons.size()); ++i)
    {
        m_bortle_buttons[static_cast<std::size_t>(i)]->set_position({sx, cy});
        sx += kSmallBtnW + kSmallBtnSpacing;
    }
    cy += kSmallBtnH + 4.0f;

    // "Current: N" — just text
    cy += kRowHeight;

    cy += 4.0f;

    // Magnitude limit slider
    m_slider_mag_limit->set_position({cx, cy});
}

// =================================================================
// Update — animation, input, sync
// =================================================================

void SidePanel::update(Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                       u32 viewport_width, u32 viewport_height,
                       const SidePanelState& state)
{
    if (!m_initialized)
    {
        return;
    }

    // Check if mouse is in the left activation zone or over the panel
    const bool in_activation = mouse_pos.x <= kActivationZone;
    const bool over_panel = is_mouse_over(mouse_pos);
    const bool any_dragging = is_dragging();

    m_mouse_in_zone = in_activation || over_panel || any_dragging;

    // Linger timer
    if (m_mouse_in_zone)
    {
        m_linger_timer = kLingerTime;
    }
    else
    {
        m_linger_timer -= dt;
        if (m_linger_timer < 0.0f)
        {
            m_linger_timer = 0.0f;
        }
    }

    const bool should_show = m_mouse_in_zone || m_linger_timer > 0.0f;

    // Animate slide
    if (should_show)
    {
        m_slide_t += kSlideSpeed * dt;
        if (m_slide_t > 1.0f) m_slide_t = 1.0f;
    }
    else
    {
        m_slide_t -= kSlideSpeed * dt;
        if (m_slide_t < 0.0f) m_slide_t = 0.0f;
    }

    // Early out if fully hidden
    if (m_slide_t <= 0.0f)
    {
        return;
    }

    // Re-layout each frame (position depends on m_slide_t)
    layout_widgets(viewport_width, viewport_height);

    // -----------------------------------------------------------------
    // Sync state from application
    // -----------------------------------------------------------------

    // Selected preset
    if (state.selected_preset >= 0 &&
        state.selected_preset < static_cast<i32>(m_preset_buttons.size()))
    {
        m_selected_preset = state.selected_preset;
    }

    // Sync preset toggle buttons
    for (i32 i = 0; i < static_cast<i32>(m_preset_buttons.size()); ++i)
    {
        m_preset_buttons[static_cast<std::size_t>(i)]->set_active(i == m_selected_preset);
    }

    // Bortle scale
    const i32 bortle_idx = static_cast<i32>(std::round(state.bortle_scale)) - 1;
    if (bortle_idx >= 0 && bortle_idx < 9)
    {
        m_selected_bortle = bortle_idx;
    }

    for (i32 i = 0; i < static_cast<i32>(m_bortle_buttons.size()); ++i)
    {
        m_bortle_buttons[static_cast<std::size_t>(i)]->set_active(i == m_selected_bortle);
    }

    // Magnitude limit slider (only if not dragging)
    if (!m_slider_mag_limit->is_dragging())
    {
        m_slider_mag_limit->set_value(state.magnitude_limit);
    }

    // Readout text
    m_lat_text  = "Lat  " + format_latitude(state.latitude_deg);
    m_lon_text  = "Lon  " + format_longitude(state.longitude_deg);

    char elev_buf[32];
    std::snprintf(elev_buf, sizeof(elev_buf), "Elev %.0f m", state.elevation_m);
    m_elev_text = elev_buf;

    m_utc_text = "UTC  " + state.utc_string;
    m_lst_text = "LST  " + state.lst_string;

    char jd_buf[32];
    std::snprintf(jd_buf, sizeof(jd_buf), "JD   %.3f", state.julian_date);
    m_jd_text = jd_buf;

    char bortle_buf[32];
    std::snprintf(bortle_buf, sizeof(bortle_buf), "Current: %d",
                  m_selected_bortle + 1);
    m_bortle_text = bortle_buf;

    // -----------------------------------------------------------------
    // Update all widgets with input
    // -----------------------------------------------------------------

    for (auto& btn : m_preset_buttons)
    {
        btn->update(mouse_pos, mouse_clicked, dt);
    }

    for (auto& btn : m_bortle_buttons)
    {
        btn->update(mouse_pos, mouse_clicked, dt);
    }

    m_slider_mag_limit->update(mouse_pos, mouse_down, mouse_clicked, dt);

    for (auto& btn : m_speed_buttons)
    {
        btn->update(mouse_pos, mouse_clicked, dt);
    }
}

// =================================================================
// Render
// =================================================================

void SidePanel::render(BitmapFont& font, rendering::LineRenderer& lines,
                       VkExtent2D extent) const
{
    if (!m_initialized || m_slide_t <= 0.001f)
    {
        return;
    }

    const Vec2f vp = {static_cast<f32>(extent.width), static_cast<f32>(extent.height)};

    // Helper: pixel → NDC
    const auto p_to_ndc = [&](Vec2f px) -> Vec2f
    {
        return {
            (px.x / vp.x) * 2.0f - 1.0f,
            (px.y / vp.y) * 2.0f - 1.0f
        };
    };

    // -----------------------------------------------------------------
    // Panel border (green rect outline)
    // -----------------------------------------------------------------
    {
        const Vec2f tl = p_to_ndc({m_panel_x, m_panel_y});
        const Vec2f tr = p_to_ndc({m_panel_x + kPanelWidth, m_panel_y});
        const Vec2f br = p_to_ndc({m_panel_x + kPanelWidth, m_panel_y + kPanelHeight});
        const Vec2f bl = p_to_ndc({m_panel_x, m_panel_y + kPanelHeight});

        lines.add_line(tl, tr, widget_colors::kBorder);
        lines.add_line(tr, br, widget_colors::kBorder);
        lines.add_line(br, bl, widget_colors::kBorder);
        lines.add_line(bl, tl, widget_colors::kBorder);
    }

    // Content origin
    const f32 cx = m_panel_x + kPadding;
    f32 cy = m_panel_y + kPadding;

    // =================================================================
    // OBSERVER section
    // =================================================================

    // Header
    font.draw_text("=== OBSERVER ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    // "Location" label
    font.draw_text("Location", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    // Preset buttons
    for (const auto& btn : m_preset_buttons)
    {
        btn->render(font, lines, vp);
        cy += kPresetBtnH + 2.0f;
    }
    cy += 4.0f;

    // Lat / Lon / Elevation readouts
    font.draw_text(m_lat_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_lon_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_elev_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    // Separator
    cy += 6.0f;
    {
        const f32 sep_y = cy - 3.0f;
        lines.add_line(p_to_ndc({cx, sep_y}),
                       p_to_ndc({cx + kPanelWidth - 2.0f * kPadding, sep_y}),
                       widget_colors::kBorder);
    }

    // =================================================================
    // TIME section
    // =================================================================

    font.draw_text("=== TIME ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_utc_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_lst_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_jd_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    cy += 4.0f;

    // Speed label
    font.draw_text("Speed", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    // Speed buttons row 1 (first 3)
    for (std::size_t i = 0; i < m_speed_buttons.size() && i < 3; ++i)
    {
        m_speed_buttons[i]->render(font, lines, vp);
    }
    cy += kSmallBtnH + kSmallBtnSpacing;

    // Speed buttons row 2 (remaining)
    for (std::size_t i = 3; i < m_speed_buttons.size(); ++i)
    {
        m_speed_buttons[i]->render(font, lines, vp);
    }
    cy += kSmallBtnH + 6.0f;

    // Separator
    {
        const f32 sep_y = cy - 3.0f;
        lines.add_line(p_to_ndc({cx, sep_y}),
                       p_to_ndc({cx + kPanelWidth - 2.0f * kPadding, sep_y}),
                       widget_colors::kBorder);
    }

    // =================================================================
    // SKY section
    // =================================================================

    font.draw_text("=== SKY ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text("Bortle", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    // Bortle row 1 (1–5)
    for (i32 i = 0; i < 5 && i < static_cast<i32>(m_bortle_buttons.size()); ++i)
    {
        m_bortle_buttons[static_cast<std::size_t>(i)]->render(font, lines, vp);
    }
    cy += kSmallBtnH + kSmallBtnSpacing;

    // Bortle row 2 (6–9)
    for (i32 i = 5; i < 9 && i < static_cast<i32>(m_bortle_buttons.size()); ++i)
    {
        m_bortle_buttons[static_cast<std::size_t>(i)]->render(font, lines, vp);
    }
    cy += kSmallBtnH + 4.0f;

    // "Current: N"
    font.draw_text(m_bortle_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    cy += 4.0f;

    // Magnitude limit slider
    m_slider_mag_limit->render(font, lines, vp);
}

// =================================================================
// Queries
// =================================================================

bool SidePanel::is_visible() const
{
    return m_slide_t > 0.001f;
}

bool SidePanel::is_mouse_over(Vec2f mouse_pos) const
{
    if (!m_initialized || m_slide_t <= 0.001f)
    {
        return false;
    }

    return mouse_pos.x >= m_panel_x &&
           mouse_pos.x <= m_panel_x + kPanelWidth &&
           mouse_pos.y >= m_panel_y &&
           mouse_pos.y <= m_panel_y + kPanelHeight;
}

bool SidePanel::is_dragging() const
{
    if (!m_initialized)
    {
        return false;
    }
    return m_slider_mag_limit && m_slider_mag_limit->is_dragging();
}

const std::vector<ObserverPreset>& SidePanel::get_presets() const
{
    return m_presets;
}

// =================================================================
// Formatting helpers
// =================================================================

std::string SidePanel::format_latitude(f64 lat_deg)
{
    const char dir = lat_deg >= 0.0 ? 'N' : 'S';
    const f64 abs_lat = std::abs(lat_deg);
    const i32 deg = static_cast<i32>(abs_lat);
    const f64 min_f = (abs_lat - deg) * 60.0;
    const i32 min = static_cast<i32>(min_f);
    const i32 sec = static_cast<i32>((min_f - min) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+03d %02d' %02d\" %c", deg, min, sec, dir);
    return buf;
}

std::string SidePanel::format_longitude(f64 lon_deg)
{
    const char dir = lon_deg >= 0.0 ? 'E' : 'W';
    const f64 abs_lon = std::abs(lon_deg);
    const i32 deg = static_cast<i32>(abs_lon);
    const f64 min_f = (abs_lon - deg) * 60.0;
    const i32 min = static_cast<i32>(min_f);
    const i32 sec = static_cast<i32>((min_f - min) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+04d %02d' %02d\" %c", deg, min, sec, dir);
    return buf;
}

} // namespace parallax::ui