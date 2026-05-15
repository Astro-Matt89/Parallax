/// @file toolbar.cpp
/// @brief Toolbar implementation — layout, animation, widget management.
///
/// SPRINT 05 Task 5.3

#include "ui/toolbar.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>

namespace parallax::ui
{

namespace
{
constexpr f32 kObserveButtonWidth = 72.0f;
constexpr f32 kSessionsButtonWidth = 74.0f;
constexpr f32 kDataButtonWidth = 56.0f;
} // namespace

// =================================================================
// Initialization
// =================================================================

void Toolbar::init(const ToolbarCallbacks& callbacks)
{
    create_overlay_group(callbacks);
    create_time_group(callbacks);
    create_view_group(callbacks);

    m_initialized = true;
    PLX_CORE_INFO("Toolbar initialized (3 groups, {} buttons, 1 slider)",
                  9 + 4);  // overlay group adds OBSERVE/SESSIONS/DATA
}

// =================================================================
// Widget creation — Overlay toggles
// =================================================================

void Toolbar::create_overlay_group(const ToolbarCallbacks& callbacks)
{
    // Retro text "icons" — short labels matching keyboard shortcuts
    // C=constellations, S=stars, D=DSO, G=grid, O=horizon, A=atmosphere
    const Vec2f dummy_pos = {0.0f, 0.0f};
    const Vec2f btn_size = {kButtonW, kButtonH};

    m_btn_const = std::make_unique<ToggleButton>(
        "CONST", dummy_pos, btn_size,
        callbacks.toggle_constellations ? callbacks.toggle_constellations : []() {});

    m_btn_stars = std::make_unique<ToggleButton>(
        "STAR", dummy_pos, btn_size,
        callbacks.toggle_stars ? callbacks.toggle_stars : []() {});

    m_btn_dso = std::make_unique<ToggleButton>(
        "DSO", dummy_pos, btn_size,
        callbacks.toggle_dso ? callbacks.toggle_dso : []() {});

    m_btn_grid = std::make_unique<ToggleButton>(
        "GRID", dummy_pos, btn_size,
        callbacks.cycle_grid ? callbacks.cycle_grid : []() {});

    m_btn_horiz = std::make_unique<ToggleButton>(
        "HORIZ", dummy_pos, Vec2f{56.0f, kButtonH},
        callbacks.toggle_horizon ? callbacks.toggle_horizon : []() {});

    // ATMO — atmosphere toggle (A key equivalent)       ← SPRINT 06 Task 6.7
    m_btn_atmo = std::make_unique<ToggleButton>(
        "ATMO", dummy_pos, btn_size,
        callbacks.toggle_atmosphere ? callbacks.toggle_atmosphere : []() {});

    m_btn_observe = std::make_unique<ToggleButton>(
        "OBSERVE", dummy_pos, Vec2f{kObserveButtonWidth, kButtonH},
        callbacks.toggle_observe_panel ? callbacks.toggle_observe_panel : []() {});

    m_btn_sessions = std::make_unique<ToggleButton>(
        "SESSIONS", dummy_pos, Vec2f{kSessionsButtonWidth, kButtonH},
        callbacks.toggle_sessions_panel ? callbacks.toggle_sessions_panel : []() {});

    m_btn_data = std::make_unique<ToggleButton>(
        "DATA", dummy_pos, Vec2f{kDataButtonWidth, kButtonH},
        callbacks.toggle_data_panel ? callbacks.toggle_data_panel : []() {});
}

// =================================================================
// Widget creation — Time controls
// =================================================================

void Toolbar::create_time_group(const ToolbarCallbacks& callbacks)
{
    const Vec2f dummy_pos = {0.0f, 0.0f};
    const Vec2f btn_size = {36.0f, kButtonH};

    // << (reverse / slower)
    m_btn_time_rev = std::make_unique<Button>(
        "<<", dummy_pos, btn_size,
        callbacks.time_reverse ? callbacks.time_reverse : []() {});

    // || (pause/resume)
    m_btn_time_pause = std::make_unique<Button>(
        "||", dummy_pos, btn_size,
        callbacks.time_pause_toggle ? callbacks.time_pause_toggle : []() {});

    // >> (forward / faster)
    m_btn_time_fwd = std::make_unique<Button>(
        ">>", dummy_pos, btn_size,
        callbacks.time_forward ? callbacks.time_forward : []() {});

    // = (reset to now)
    m_btn_time_now = std::make_unique<Button>(
        "NOW", dummy_pos, btn_size,
        callbacks.time_reset_now ? callbacks.time_reset_now : []() {});

    m_speed_text = "x1";
}

// =================================================================
// Widget creation — View controls
// =================================================================

void Toolbar::create_view_group(const ToolbarCallbacks& callbacks)
{
    const Vec2f dummy_pos = {0.0f, 0.0f};

    m_slider_fov = std::make_unique<Slider>(
        "FOV", dummy_pos, 200.0f,
        0.5f, 120.0f, 0.5f);
    m_slider_fov->set_value(60.0f);
    m_slider_fov->set_format_string("{:.0f}");

    if (callbacks.set_fov)
    {
        m_slider_fov->on_value_changed = [cb = callbacks.set_fov](f32 val)
        {
            cb(static_cast<f64>(val));
        };
    }
}

// =================================================================
// Layout — position all widgets based on viewport size
// =================================================================

void Toolbar::layout_widgets(u32 viewport_width, u32 viewport_height)
{
    m_viewport_w = viewport_width;
    m_viewport_h = viewport_height;

    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);

    // Toolbar sits at the bottom, centered horizontally
    // Compute total width of all groups

    // Group 1: overlay toggles + observation panel toggles.
    const f32 overlay_w = kButtonW * 5.0f + 56.0f + kButtonSpacing * 8.0f
                           + kButtonW + kObserveButtonWidth + kSessionsButtonWidth + kDataButtonWidth;

    // Group 2: time controls (4 buttons + speed text)
    const f32 time_btn_w = 36.0f;
    const f32 speed_text_w = 64.0f;  // "x10000" max
    const f32 time_w = time_btn_w * 4.0f + kButtonSpacing * 4.0f + speed_text_w;

    // Group 3: view (FOV slider)
    const f32 view_w = 200.0f;

    m_toolbar_w = overlay_w + kGroupSpacing + time_w + kGroupSpacing + view_w + 16.0f;
    m_toolbar_x = (vw - m_toolbar_w) * 0.5f;

    // Animated Y position: slides up from below
    const f32 hidden_y = vh;
    const f32 visible_y = vh - kToolbarHeight;
    m_toolbar_y = hidden_y + (visible_y - hidden_y) * m_slide_t;

    // Widget Y: centered vertically in toolbar
    const f32 widget_y = m_toolbar_y + (kToolbarHeight - kButtonH) * 0.5f;
    f32 x = m_toolbar_x + 8.0f;

    // --- Group 1: Overlay toggles ---
    m_btn_const->set_position({x, widget_y});
    x += kButtonW + kButtonSpacing;

    m_btn_stars->set_position({x, widget_y});
    x += kButtonW + kButtonSpacing;

    m_btn_dso->set_position({x, widget_y});
    x += kButtonW + kButtonSpacing;

    m_btn_grid->set_position({x, widget_y});
    x += kButtonW + kButtonSpacing;

    m_btn_horiz->set_position({x, widget_y});
    x += 56.0f + kButtonSpacing;

    // ATMO button (atmosphere toggle)                   ← SPRINT 06 Task 6.7
    m_btn_atmo->set_position({x, widget_y});
    x += kButtonW + kButtonSpacing;

    m_btn_observe->set_position({x, widget_y});
    x += kObserveButtonWidth + kButtonSpacing;

    m_btn_sessions->set_position({x, widget_y});
    x += kSessionsButtonWidth + kButtonSpacing;

    m_btn_data->set_position({x, widget_y});
    x += kDataButtonWidth + kGroupSpacing;

    // --- Group 2: Time controls ---
    m_btn_time_rev->set_position({x, widget_y});
    x += 36.0f + kButtonSpacing;

    m_btn_time_pause->set_position({x, widget_y});
    x += 36.0f + kButtonSpacing;

    m_btn_time_fwd->set_position({x, widget_y});
    x += 36.0f + kButtonSpacing;

    m_btn_time_now->set_position({x, widget_y});
    x += 36.0f + kButtonSpacing;

    // Speed text is positioned after time buttons (rendered as text, not a widget)
    // m_speed_text_x = x; (rendered in render())
    x += speed_text_w + kGroupSpacing;

    // --- Group 3: View ---
    m_slider_fov->set_position({x, widget_y});
}

// =================================================================
// Update — animation, input, sync
// =================================================================

void Toolbar::update(Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                     u32 viewport_width, u32 viewport_height,
                     const ToolbarState& state)
{
    if (!m_initialized)
    {
        return;
    }

    const f32 vh = static_cast<f32>(viewport_height);

    // Check if mouse is in the activation zone or over the toolbar
    const bool in_activation = mouse_pos.y >= (vh - kActivationZone);
    const bool over_toolbar = is_mouse_over(mouse_pos);
    const bool any_dragging = is_dragging();

    m_mouse_in_zone = in_activation || over_toolbar || any_dragging;

    // Linger timer — keep toolbar visible briefly after mouse leaves
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

    // Re-layout if viewport changed or each frame (position depends on m_slide_t)
    layout_widgets(viewport_width, viewport_height);

    // Sync toggle states from application
    m_btn_const->set_active(state.constellations_visible);
    m_btn_stars->set_active(state.stars_visible);
    m_btn_dso->set_active(state.dso_visible);
    m_btn_grid->set_active(state.grid_visible);
    m_btn_horiz->set_active(state.horizon_visible);
    m_btn_atmo->set_active(state.atmosphere_on);        // ← SPRINT 06 Task 6.7
    m_btn_observe->set_active(state.observe_panel_visible);
    m_btn_sessions->set_active(state.sessions_panel_visible);
    m_btn_data->set_active(state.data_panel_visible);

    // Sync FOV slider (only if not currently dragging)
    if (!m_slider_fov->is_dragging())
    {
        m_slider_fov->set_value(static_cast<f32>(state.fov_deg));
    }

    // Update speed display text
    if (state.time_paused)
    {
        m_speed_text = "PAUSED";
    }
    else
    {
        const auto abs_scale = std::abs(state.time_scale);
        if (abs_scale >= 10000.0)
        {
            m_speed_text = (state.time_scale < 0.0 ? "-" : "") +
                std::string("x") + std::to_string(static_cast<int>(abs_scale));
        }
        else if (abs_scale >= 1000.0)
        {
            m_speed_text = (state.time_scale < 0.0 ? "-" : "") +
                std::string("x") + std::to_string(static_cast<int>(abs_scale));
        }
        else if (abs_scale >= 100.0)
        {
            m_speed_text = (state.time_scale < 0.0 ? "-" : "") +
                std::string("x") + std::to_string(static_cast<int>(abs_scale));
        }
        else if (abs_scale >= 10.0)
        {
            m_speed_text = (state.time_scale < 0.0 ? "-" : "") +
                std::string("x") + std::to_string(static_cast<int>(abs_scale));
        }
        else
        {
            m_speed_text = (state.time_scale < 0.0 ? "-" : "") + std::string("x1");
        }
    }

    // Update pause button label to reflect state
    m_btn_time_pause->set_label(state.time_paused ? ">>" : "||");

    // Update all widgets with input
    m_btn_const->update(mouse_pos, mouse_clicked, dt);
    m_btn_stars->update(mouse_pos, mouse_clicked, dt);
    m_btn_dso->update(mouse_pos, mouse_clicked, dt);
    m_btn_grid->update(mouse_pos, mouse_clicked, dt);
    m_btn_horiz->update(mouse_pos, mouse_clicked, dt);
    m_btn_atmo->update(mouse_pos, mouse_clicked, dt);   // ← SPRINT 06 Task 6.7
    m_btn_observe->update(mouse_pos, mouse_clicked, dt);
    m_btn_sessions->update(mouse_pos, mouse_clicked, dt);
    m_btn_data->update(mouse_pos, mouse_clicked, dt);

    m_btn_time_rev->update(mouse_pos, mouse_clicked, dt);
    m_btn_time_pause->update(mouse_pos, mouse_clicked, dt);
    m_btn_time_fwd->update(mouse_pos, mouse_clicked, dt);
    m_btn_time_now->update(mouse_pos, mouse_clicked, dt);

    m_slider_fov->update(mouse_pos, mouse_down, mouse_clicked, dt);
}

// =================================================================
// Render
// =================================================================

void Toolbar::render(BitmapFont& font, rendering::LineRenderer& lines,
                     PanelSystem& /*panel_system*/,
                     VkCommandBuffer /*cmd*/, VkExtent2D extent) const
{
    if (!m_initialized || m_slide_t <= 0.001f)
    {
        return;
    }

    const Vec2f vp = {static_cast<f32>(extent.width), static_cast<f32>(extent.height)};

    // --- Render all overlay toggle buttons ---
    m_btn_const->render(font, lines, vp);
    m_btn_stars->render(font, lines, vp);
    m_btn_dso->render(font, lines, vp);
    m_btn_grid->render(font, lines, vp);
    m_btn_horiz->render(font, lines, vp);
    m_btn_atmo->render(font, lines, vp);    // ← SPRINT 06 Task 6.7
    m_btn_observe->render(font, lines, vp);
    m_btn_sessions->render(font, lines, vp);
    m_btn_data->render(font, lines, vp);

    // --- Group separator line 1 ---
    {
        const f32 sep_x = m_btn_data->get_position().x + m_btn_data->get_size().x
                          + kGroupSpacing * 0.5f;
        const f32 sep_top = m_toolbar_y + 6.0f;
        const f32 sep_bot = m_toolbar_y + kToolbarHeight - 6.0f;

        const auto p_to_ndc = [&](Vec2f px) -> Vec2f
        {
            return {
                (px.x / vp.x) * 2.0f - 1.0f,
                (px.y / vp.y) * 2.0f - 1.0f
            };
        };

        lines.add_line(p_to_ndc({sep_x, sep_top}),
                       p_to_ndc({sep_x, sep_bot}),
                       widget_colors::kBorder);
    }

    // --- Render time control buttons ---
    m_btn_time_rev->render(font, lines, vp);
    m_btn_time_pause->render(font, lines, vp);
    m_btn_time_fwd->render(font, lines, vp);
    m_btn_time_now->render(font, lines, vp);

    // --- Speed text ---
    {
        const f32 text_x = m_btn_time_now->get_position().x
                           + m_btn_time_now->get_size().x + 8.0f;
        const f32 text_y = m_toolbar_y + (kToolbarHeight - 16.0f) * 0.5f;
        font.draw_text(m_speed_text, text_x, text_y, 1.0f, widget_colors::kTextBright);
    }

    // --- Group separator line 2 ---
    {
        const f32 sep_x = m_btn_time_now->get_position().x
                          + m_btn_time_now->get_size().x + 64.0f + kGroupSpacing * 0.5f;
        const f32 sep_top = m_toolbar_y + 6.0f;
        const f32 sep_bot = m_toolbar_y + kToolbarHeight - 6.0f;

        const auto p_to_ndc = [&](Vec2f px) -> Vec2f
        {
            return {
                (px.x / vp.x) * 2.0f - 1.0f,
                (px.y / vp.y) * 2.0f - 1.0f
            };
        };

        lines.add_line(p_to_ndc({sep_x, sep_top}),
                       p_to_ndc({sep_x, sep_bot}),
                       widget_colors::kBorder);
    }

    // --- Render FOV slider ---
    m_slider_fov->render(font, lines, vp);
}

// =================================================================
// Queries
// =================================================================

bool Toolbar::is_visible() const
{
    return m_slide_t > 0.001f;
}

bool Toolbar::is_mouse_over(Vec2f mouse_pos) const
{
    if (!m_initialized || m_slide_t <= 0.001f)
    {
        return false;
    }

    return mouse_pos.x >= m_toolbar_x &&
           mouse_pos.x <= m_toolbar_x + m_toolbar_w &&
           mouse_pos.y >= m_toolbar_y &&
           mouse_pos.y <= m_toolbar_y + kToolbarHeight;
}

bool Toolbar::is_dragging() const
{
    if (!m_initialized)
    {
        return false;
    }
    return m_slider_fov && m_slider_fov->is_dragging();
}

} // namespace parallax::ui
