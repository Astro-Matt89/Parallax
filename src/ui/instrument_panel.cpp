/// @file instrument_panel.cpp
/// @brief Observation scheduling panel implementation.

#include "ui/instrument_panel.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace
{
constexpr double kRadToDeg = 57.2957795130823208768;
constexpr double kDegToRad = 0.01745329251994329577;
}

namespace parallax::ui
{

void InstrumentPanel::init(const InstrumentPanelCallbacks& callbacks)
{
    m_callbacks = callbacks;

    const Vec2f dummy = {0.0f, 0.0f};
    const Vec2f btn_medium = {120.0f, 28.0f};
    const Vec2f btn_wide = {140.0f, 28.0f};

    m_btn_target_selected = std::make_unique<Button>(
        "SELECTED", dummy, btn_medium, [this]() { m_target_mode = TargetMode::SelectedObject; });
    m_btn_target_survey = std::make_unique<Button>(
        "SURVEY", dummy, btn_medium, [this]() { m_target_mode = TargetMode::SurveyScan; });

    m_btn_technique_cycle = std::make_unique<Button>(
        "mock", dummy, btn_wide, [this]()
        {
            if (!m_techniques.empty())
            {
                m_technique_index = (m_technique_index + 1) % static_cast<i32>(m_techniques.size());
                m_btn_technique_cycle->set_label(m_techniques[static_cast<std::size_t>(m_technique_index)]);
            }
        });

    m_btn_schedule = std::make_unique<Button>("SCHEDULE", dummy, btn_wide, []() {});
    m_btn_cancel   = std::make_unique<Button>("CANCEL", dummy, btn_wide, [this]() { m_visible = false; });

    m_slider_duration = std::make_unique<Slider>("Duration(h)", dummy, 260.0f, 0.5f, 24.0f, 0.5f);
    m_slider_duration->set_value(m_duration_hours);
    m_slider_duration->set_format_string("{:.1f}");

    m_slider_ra_deg = std::make_unique<Slider>("RA(deg)", dummy, 300.0f, 0.0f, 360.0f, 0.1f);
    m_slider_dec_deg = std::make_unique<Slider>("Dec(deg)", dummy, 300.0f, -90.0f, 90.0f, 0.1f);
    m_slider_radius_deg = std::make_unique<Slider>("Radius(deg)", dummy, 300.0f, 0.1f, 90.0f, 0.1f);

    m_techniques = {"mock", "photometry", "spectroscopy"};
    m_technique_index = 0;
    m_btn_technique_cycle->set_label(m_techniques[0]);

    m_btn_schedule->set_label("SCHEDULE");
    m_initialized = true;
    PLX_CORE_INFO("InstrumentPanel initialized");
}

void InstrumentPanel::set_visible(bool visible)
{
    if (visible && !m_visible)
    {
        m_pending_defaults = true;
    }
    m_visible = visible;
}

void InstrumentPanel::open_for_selected_object(u64 target_object_id)
{
    m_open_target_override = target_object_id;
    m_target_mode = TargetMode::SelectedObject;
    m_pending_defaults = true;
    m_visible = true;
}

bool InstrumentPanel::is_visible() const
{
    return m_visible;
}

bool InstrumentPanel::is_mouse_over(Vec2f mouse_pos) const
{
    if (!m_visible)
    {
        return false;
    }

    return mouse_pos.x >= m_panel_x
        && mouse_pos.x <= m_panel_x + kPanelWidth
        && mouse_pos.y >= m_panel_y
        && mouse_pos.y <= m_panel_y + kPanelHeight;
}

void InstrumentPanel::layout_widgets(u32 viewport_width, u32 viewport_height)
{
    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);

    m_panel_x = (vw - kPanelWidth) * 0.5f;
    m_panel_y = (vh - kPanelHeight) * 0.5f;

    const f32 cx = m_panel_x + kPadding;
    f32 cy = m_panel_y + kPadding + kRowHeight * 4.0f;

    m_btn_target_selected->set_position({cx, cy});
    m_btn_target_survey->set_position({cx + 130.0f, cy});
    cy += 34.0f;

    m_slider_ra_deg->set_position({cx, cy});
    cy += 24.0f;
    m_slider_dec_deg->set_position({cx, cy});
    cy += 24.0f;
    m_slider_radius_deg->set_position({cx, cy});
    cy += 28.0f;

    m_btn_technique_cycle->set_position({cx, cy});
    cy += 36.0f;

    m_slider_duration->set_position({cx, cy});

    const f32 btn_y = m_panel_y + kPanelHeight - kPadding - 28.0f;
    m_btn_schedule->set_position({m_panel_x + kPadding, btn_y});
    m_btn_cancel->set_position({m_panel_x + kPadding + 150.0f, btn_y});
}

void InstrumentPanel::apply_open_defaults(const InstrumentPanelState& state)
{
    if (!m_pending_defaults)
    {
        return;
    }

    m_duration_hours = 1.0f;
    m_slider_duration->set_value(m_duration_hours);

    m_survey_center_ra_deg = static_cast<f32>(state.center_ra_rad * kRadToDeg);
    while (m_survey_center_ra_deg < 0.0f)
    {
        m_survey_center_ra_deg += 360.0f;
    }
    while (m_survey_center_ra_deg >= 360.0f)
    {
        m_survey_center_ra_deg -= 360.0f;
    }

    m_survey_center_dec_deg = static_cast<f32>(state.center_dec_rad * kRadToDeg);
    m_survey_radius_deg = std::max(0.1f, static_cast<f32>(state.fov_rad * kRadToDeg * 0.5));

    m_slider_ra_deg->set_value(m_survey_center_ra_deg);
    m_slider_dec_deg->set_value(m_survey_center_dec_deg);
    m_slider_radius_deg->set_value(m_survey_radius_deg);

    if (m_open_target_override.has_value())
    {
        m_target_mode = TargetMode::SelectedObject;
    }

    m_pending_defaults = false;
}

void InstrumentPanel::update(const InstrumentPanelState& state,
                             Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                             u32 viewport_width, u32 viewport_height)
{
    if (!m_initialized || !m_visible)
    {
        return;
    }

    layout_widgets(viewport_width, viewport_height);
    apply_open_defaults(state);

    m_current_selected_id = state.selected_object_id;
    m_current_selected_name = state.selected_object_name;

    m_btn_target_selected->update(mouse_pos, mouse_clicked, dt);
    m_btn_target_survey->update(mouse_pos, mouse_clicked, dt);
    m_btn_technique_cycle->update(mouse_pos, mouse_clicked, dt);
    m_btn_schedule->update(mouse_pos, mouse_clicked, dt);
    m_btn_cancel->update(mouse_pos, mouse_clicked, dt);

    m_slider_duration->update(mouse_pos, mouse_down, mouse_clicked, dt);
    m_duration_hours = m_slider_duration->get_value();

    if (m_target_mode == TargetMode::SurveyScan)
    {
        m_slider_ra_deg->update(mouse_pos, mouse_down, mouse_clicked, dt);
        m_slider_dec_deg->update(mouse_pos, mouse_down, mouse_clicked, dt);
        m_slider_radius_deg->update(mouse_pos, mouse_down, mouse_clicked, dt);

        m_survey_center_ra_deg = m_slider_ra_deg->get_value();
        m_survey_center_dec_deg = m_slider_dec_deg->get_value();
        m_survey_radius_deg = m_slider_radius_deg->get_value();
    }

    if (m_btn_schedule->is_hovered() && mouse_clicked)
    {
        observation::SessionParameters params;
        params.instrument_id = 1;
        params.planned_duration_hours = m_duration_hours;
        params.start_julian_date = state.current_julian_date;
        params.technique = m_techniques[static_cast<std::size_t>(m_technique_index)];

        if (m_target_mode == TargetMode::SelectedObject)
        {
            params.type = observation::SessionType::PointedObservation;
            params.target_object_id = m_open_target_override.value_or(state.selected_object_id);
            if (params.target_object_id == 0)
            {
                return;
            }
        }
        else
        {
            params.type = observation::SessionType::SurveyScan;
            params.target_object_id = 0;
            params.target_region.center_ra = static_cast<double>(m_survey_center_ra_deg) * kDegToRad;
            params.target_region.center_dec = static_cast<double>(m_survey_center_dec_deg) * kDegToRad;
            params.target_region.radius_rad = static_cast<double>(m_survey_radius_deg) * kDegToRad;
        }

        if (m_callbacks.schedule)
        {
            m_callbacks.schedule(params);
            m_visible = false;
            m_open_target_override.reset();
        }
    }
}

void InstrumentPanel::render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const
{
    if (!m_initialized || !m_visible)
    {
        return;
    }

    const Vec2f vp = {static_cast<f32>(extent.width), static_cast<f32>(extent.height)};
    const auto p_to_ndc = [&](Vec2f px) -> Vec2f
    {
        return {(px.x / vp.x) * 2.0f - 1.0f, (px.y / vp.y) * 2.0f - 1.0f};
    };

    const Vec2f tl = p_to_ndc({m_panel_x, m_panel_y});
    const Vec2f tr = p_to_ndc({m_panel_x + kPanelWidth, m_panel_y});
    const Vec2f br = p_to_ndc({m_panel_x + kPanelWidth, m_panel_y + kPanelHeight});
    const Vec2f bl = p_to_ndc({m_panel_x, m_panel_y + kPanelHeight});
    lines.add_line(tl, tr, widget_colors::kBorder);
    lines.add_line(tr, br, widget_colors::kBorder);
    lines.add_line(br, bl, widget_colors::kBorder);
    lines.add_line(bl, tl, widget_colors::kBorder);

    const f32 cx = m_panel_x + kPadding;
    f32 cy = m_panel_y + kPadding;

    font.draw_text("=== OBSERVE ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;
    font.draw_text("Instrument: Magic Instrument", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    const char* mode_text = (m_target_mode == TargetMode::SelectedObject)
        ? "Target Mode: Selected Object"
        : "Target Mode: Survey Scan";
    font.draw_text(mode_text, cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    if (m_target_mode == TargetMode::SurveyScan)
    {
        font.draw_text("Survey region fields below use degrees", cx, cy, 1.0f, widget_colors::kTextDim);
    }
    else
    {
        const u64 target_id = m_open_target_override.value_or(m_current_selected_id);
        const std::string target_text = std::format("Target: {} ({})",
                                                    m_current_selected_name.empty() ? "(none)" : m_current_selected_name,
                                                    target_id);
        font.draw_text(target_text, cx, cy, 1.0f, widget_colors::kTextDim);
    }

    m_btn_target_selected->render(font, lines, vp);
    m_btn_target_survey->render(font, lines, vp);
    m_btn_technique_cycle->render(font, lines, vp);
    m_slider_duration->render(font, lines, vp);

    if (m_target_mode == TargetMode::SurveyScan)
    {
        m_slider_ra_deg->render(font, lines, vp);
        m_slider_dec_deg->render(font, lines, vp);
        m_slider_radius_deg->render(font, lines, vp);
    }

    m_btn_schedule->render(font, lines, vp);
    m_btn_cancel->render(font, lines, vp);
}

} // namespace parallax::ui
