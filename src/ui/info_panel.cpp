/// @file info_panel.cpp
/// @brief Right-side info panel implementation.
///
/// SPRINT 05 Task 5.5

#include "ui/info_panel.hpp"

#include "core/logger.hpp"
#include "core/types.hpp"

#include <cmath>
#include <cstdio>

namespace parallax::ui
{

// =================================================================
// Initialization
// =================================================================

void InfoPanel::init(const InfoPanelCallbacks& callbacks)
{
    const Vec2f dummy = {0.0f, 0.0f};
    const Vec2f btn_size = {120.0f, 28.0f};

    m_btn_track = std::make_unique<Button>(
        "TRACK", dummy, btn_size,
        callbacks.track ? callbacks.track : []() {});

    m_btn_goto = std::make_unique<Button>(
        "GOTO", dummy, btn_size,
        callbacks.goto_object ? callbacks.goto_object : []() {});

    m_initialized = true;
    PLX_CORE_INFO("InfoPanel initialized (TRACK + GOTO buttons)");
}

// =================================================================
// Layout
// =================================================================

void InfoPanel::layout_widgets(u32 viewport_width, u32 viewport_height)
{
    m_viewport_w = viewport_width;
    m_viewport_h = viewport_height;

    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);

    // Animated X position: slides in from the right
    const f32 hidden_x = vw;
    const f32 visible_x = vw - kPanelWidth;
    m_panel_x = hidden_x + (visible_x - hidden_x) * m_slide_t;

    // Vertically centered
    m_panel_y = (vh - kPanelHeight) * 0.5f;
    if (m_panel_y < 4.0f)
    {
        m_panel_y = 4.0f;
    }

    // Button positions at the bottom of the panel
    const f32 cx = m_panel_x + kPadding;
    const f32 btn_y = m_panel_y + kPanelHeight - kPadding - 28.0f;

    m_btn_track->set_position({cx, btn_y});
    m_btn_goto->set_position({cx + 130.0f, btn_y});
}

// =================================================================
// Update
// =================================================================

void InfoPanel::update(const Selection& selection,
                       Vec2f mouse_pos, bool mouse_clicked, f32 dt,
                       u32 viewport_width, u32 viewport_height)
{
    if (!m_initialized)
    {
        return;
    }

    m_should_show = selection.has_selection();

    // Animate slide
    if (m_should_show)
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

    layout_widgets(viewport_width, viewport_height);

    // -----------------------------------------------------------------
    // Build display text from selection
    // -----------------------------------------------------------------
    const auto& sel = selection.get_selection();
    m_display_type = sel.type;

    if (sel.type == SelectedObjectType::Star)
    {
        // Title: common name, procedural designation, or HIP ID fallback
        if (!sel.common_name.empty())
        {
            m_title = sel.common_name;
        }
        else if (sel.is_procedural)
        {
            m_title = sel.designation;   // e.g. "PRC-00A1F3B902"
        }
        else
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "HIP %u", sel.hip_id);
            m_title = buf;
        }

        // Subtitle: Bayer for real stars; "Procedural" for procedural stars
        m_subtitle = sel.is_procedural ? std::string{"Procedural"} : sel.bayer;

        m_type_text = sel.is_procedural ? "Star (procedural)" : "Star";

        m_ra_text = "RA   " + format_ra(sel.ra_rad);
        m_dec_text = "Dec  " + format_dec(sel.dec_rad);
        m_alt_text = "Alt  " + format_alt(sel.alt_rad);
        m_az_text = "Az   " + format_az(sel.az_rad);

        char mag_buf[32];
        std::snprintf(mag_buf, sizeof(mag_buf), "Vmag %.2f", sel.mag_v);
        m_mag_text = mag_buf;

        char bv_buf[32];
        std::snprintf(bv_buf, sizeof(bv_buf), "B-V  %+.3f", sel.color_bv);
        m_bv_text = bv_buf;

        if (sel.is_procedural)
        {
            m_spectral_text.clear();
            m_constellation_text.clear();
        }
        else
        {
            m_spectral_text      = sel.spectral_type.empty() ? "" : "Sp   " + sel.spectral_type;
            m_constellation_text = sel.constellation.empty() ? "" : "Con  " + sel.constellation;
        }
        m_size_text.clear();
        m_dist_text.clear();
        m_phase_text.clear();
        m_illum_text.clear();
    }
    else if (sel.type == SelectedObjectType::Dso)
    {
        m_title = sel.dso_common_name.empty() ? sel.designation : sel.dso_common_name;
        m_subtitle = sel.designation;
        m_type_text = catalog::dso_type_name(sel.dso_type);

        m_ra_text = "RA   " + format_ra(sel.ra_rad);
        m_dec_text = "Dec  " + format_dec(sel.dec_rad);
        m_alt_text = "Alt  " + format_alt(sel.alt_rad);
        m_az_text = "Az   " + format_az(sel.az_rad);

        char mag_buf[32];
        std::snprintf(mag_buf, sizeof(mag_buf), "Vmag %.1f", sel.mag_v);
        m_mag_text = mag_buf;

        m_bv_text.clear();
        m_spectral_text.clear();
        m_constellation_text.clear();

        char size_buf[32];
        std::snprintf(size_buf, sizeof(size_buf), "Size %.1f'", sel.size_arcmin);
        m_size_text = size_buf;

        m_dist_text.clear();
        m_phase_text.clear();
        m_illum_text.clear();
    }
    else if (sel.type == SelectedObjectType::SolarSystem)
    {
        m_title = sel.body_name;
        m_subtitle.clear();
        m_type_text = "Solar System";

        m_ra_text = "RA   " + format_ra(sel.ra_rad);
        m_dec_text = "Dec  " + format_dec(sel.dec_rad);
        m_alt_text = "Alt  " + format_alt(sel.alt_rad);
        m_az_text = "Az   " + format_az(sel.az_rad);

        // Magnitude with explicit sign for bright objects
        {
            char mag_buf[32];
            std::snprintf(mag_buf, sizeof(mag_buf), "Mag: %+.2f", sel.mag_v);
            m_mag_text = mag_buf;
        }

        m_bv_text.clear();
        m_spectral_text.clear();
        m_constellation_text.clear();

        // Distance: Moon in km, others in AU
        // Use named constants from SolarSystemRenderer (available via selection.hpp include chain).
        // body_id 0 = Sun, 1 = Moon — see SolarSystemRenderer::kBodyIdSun / kBodyIdMoon
        {
            static constexpr double kAuToKm = 149597870.7;  // km per AU (IAU 2012)
            char dist_buf[64];
            if (sel.body_id == rendering::SolarSystemRenderer::kBodyIdMoon)
            {
                const double km = sel.distance_au * kAuToKm;
                std::snprintf(dist_buf, sizeof(dist_buf), "Dist: %.0f km", km);
            }
            else
            {
                std::snprintf(dist_buf, sizeof(dist_buf), "Dist: %.3f AU", sel.distance_au);
            }
            m_dist_text = dist_buf;
        }

        // Angular diameter: arcsec if < 120", else arcmin
        {
            char size_buf[32];
            if (sel.angular_diameter_arcsec < 120.0f)
            {
                std::snprintf(size_buf, sizeof(size_buf), "Size: %.1f\"", sel.angular_diameter_arcsec);
            }
            else
            {
                std::snprintf(size_buf, sizeof(size_buf), "Size: %.1f'", sel.angular_diameter_arcsec / 60.0f);
            }
            m_size_text = size_buf;
        }

        // Phase + illumination: only for non-Sun (body_id 0 = Sun)
        if (sel.body_id != rendering::SolarSystemRenderer::kBodyIdSun)
        {
            char phase_buf[32];
            std::snprintf(phase_buf, sizeof(phase_buf), "Phase: %.1f deg", sel.phase_angle_deg);
            m_phase_text = phase_buf;

            char illum_buf[32];
            std::snprintf(illum_buf, sizeof(illum_buf), "Illum: %.1f%%", sel.illumination * 100.0f);
            m_illum_text = illum_buf;
        }
        else
        {
            m_phase_text.clear();
            m_illum_text.clear();
        }
    }

    // Tracking status
    m_tracking_text = selection.is_tracking() ? ">> TRACKING <<" : "";

    // -----------------------------------------------------------------
    // Update buttons
    // -----------------------------------------------------------------
    m_btn_track->set_label(selection.is_tracking() ? "UNTRACK" : "TRACK");
    m_btn_track->update(mouse_pos, mouse_clicked, dt);
    m_btn_goto->update(mouse_pos, mouse_clicked, dt);
}

// =================================================================
// Render
// =================================================================

void InfoPanel::render(BitmapFont& font, rendering::LineRenderer& lines,
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
    // Panel border
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

    const f32 cx = m_panel_x + kPadding;
    f32 cy = m_panel_y + kPadding;

    // -----------------------------------------------------------------
    // Header section
    // -----------------------------------------------------------------
    font.draw_text("=== SELECTED ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight + 2.0f;

    // Title (bright, large-ish)
    if (!m_title.empty())
    {
        font.draw_text(m_title, cx, cy, 1.0f, {1.0f, 1.0f, 0.0f});  // Yellow
        cy += kRowHeight;
    }

    // Subtitle
    if (!m_subtitle.empty())
    {
        font.draw_text(m_subtitle, cx, cy, 1.0f, widget_colors::kTextDim);
        cy += kRowHeight;
    }

    // Type
    if (!m_type_text.empty())
    {
        font.draw_text(m_type_text, cx, cy, 1.0f, widget_colors::kTextDim);
        cy += kRowHeight;
    }

    cy += 4.0f;

    // Separator
    {
        const f32 sep_y = cy - 2.0f;
        lines.add_line(p_to_ndc({cx, sep_y}),
                       p_to_ndc({cx + kPanelWidth - 2.0f * kPadding, sep_y}),
                       widget_colors::kBorder);
    }

    // -----------------------------------------------------------------
    // Coordinates section
    // -----------------------------------------------------------------
    font.draw_text("Equatorial (J2000)", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    font.draw_text(m_ra_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_dec_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    cy += 4.0f;

    font.draw_text("Horizontal", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    font.draw_text(m_alt_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    font.draw_text(m_az_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    cy += 4.0f;

    // Separator
    {
        const f32 sep_y = cy - 2.0f;
        lines.add_line(p_to_ndc({cx, sep_y}),
                       p_to_ndc({cx + kPanelWidth - 2.0f * kPadding, sep_y}),
                       widget_colors::kBorder);
    }

    // -----------------------------------------------------------------
    // Properties section
    // -----------------------------------------------------------------
    font.draw_text("Properties", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    font.draw_text(m_mag_text, cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    if (!m_bv_text.empty())
    {
        font.draw_text(m_bv_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_spectral_text.empty())
    {
        font.draw_text(m_spectral_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_constellation_text.empty())
    {
        font.draw_text(m_constellation_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_size_text.empty())
    {
        font.draw_text(m_size_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_dist_text.empty())
    {
        font.draw_text(m_dist_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_phase_text.empty())
    {
        font.draw_text(m_phase_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    if (!m_illum_text.empty())
    {
        font.draw_text(m_illum_text, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }

    cy += 4.0f;

    // Tracking status
    if (!m_tracking_text.empty())
    {
        font.draw_text(m_tracking_text, cx, cy, 1.0f, {1.0f, 1.0f, 0.0f});
        cy += kRowHeight;
    }

    // -----------------------------------------------------------------
    // Buttons
    // -----------------------------------------------------------------
    m_btn_track->render(font, lines, vp);
    m_btn_goto->render(font, lines, vp);
}

// =================================================================
// Queries
// =================================================================

bool InfoPanel::is_visible() const
{
    return m_slide_t > 0.001f;
}

bool InfoPanel::is_mouse_over(Vec2f mouse_pos) const
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

// =================================================================
// Formatting helpers
// =================================================================

std::string InfoPanel::format_ra(f64 ra_rad)
{
    using namespace parallax::astro_constants;
    const f64 hours = ra_rad * kRadToHour;
    const f64 abs_h = std::abs(hours);
    const i32 h = static_cast<i32>(abs_h);
    const f64 min_f = (abs_h - h) * 60.0;
    const i32 m = static_cast<i32>(min_f);
    const f64 s = (min_f - m) * 60.0;

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02dh %02dm %04.1fs", h, m, s);
    return buf;
}

std::string InfoPanel::format_dec(f64 dec_rad)
{
    using namespace parallax::astro_constants;
    const f64 deg = dec_rad * kRadToDeg;
    const char sign = deg >= 0.0 ? '+' : '-';
    const f64 abs_deg = std::abs(deg);
    const i32 d = static_cast<i32>(abs_deg);
    const f64 min_f = (abs_deg - d) * 60.0;
    const i32 m = static_cast<i32>(min_f);
    const i32 s = static_cast<i32>((min_f - m) * 60.0);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%02d %02d' %02d\"", sign, d, m, s);
    return buf;
}

std::string InfoPanel::format_alt(f64 alt_rad)
{
    using namespace parallax::astro_constants;
    const f64 deg = alt_rad * kRadToDeg;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%+.2f deg", deg);
    return buf;
}

std::string InfoPanel::format_az(f64 az_rad)
{
    using namespace parallax::astro_constants;
    f64 deg = az_rad * kRadToDeg;
    if (deg < 0.0) deg += 360.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f deg", deg);
    return buf;
}

} // namespace parallax::ui