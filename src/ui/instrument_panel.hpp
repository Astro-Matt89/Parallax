#pragma once

/// @file instrument_panel.hpp
/// @brief Observation scheduling panel (Sprint 08 Task 8.10).

#include "core/types.hpp"
#include "observation/session_types.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace parallax::ui
{

/// @brief Snapshot of current app/view state needed by InstrumentPanel.
struct InstrumentPanelState
{
    bool has_selection = false;
    u64 selected_object_id = 0;
    std::string selected_object_name;

    f64 center_ra_rad = 0.0;
    f64 center_dec_rad = 0.0;
    f64 fov_rad = 0.0;
    f64 current_julian_date = 0.0;
};

/// @brief Callbacks from InstrumentPanel to Application.
struct InstrumentPanelCallbacks
{
    std::function<void(const observation::SessionParameters&)> schedule;
};

class InstrumentPanel
{
public:
    InstrumentPanel() = default;
    ~InstrumentPanel() = default;

    InstrumentPanel(const InstrumentPanel&) = delete;
    InstrumentPanel& operator=(const InstrumentPanel&) = delete;
    InstrumentPanel(InstrumentPanel&&) = delete;
    InstrumentPanel& operator=(InstrumentPanel&&) = delete;

    void init(const InstrumentPanelCallbacks& callbacks);

    void update(const InstrumentPanelState& state,
                Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                u32 viewport_width, u32 viewport_height);

    void render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const;

    void set_visible(bool visible);
    void open_for_selected_object(u64 target_object_id);

    [[nodiscard]] bool is_visible() const;
    [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

private:
    void layout_widgets(u32 viewport_width, u32 viewport_height);
    void apply_open_defaults(const InstrumentPanelState& state);

    static constexpr f32 kPanelWidth = 420.0f;
    static constexpr f32 kPanelHeight = 380.0f;
    static constexpr f32 kPadding = 10.0f;
    static constexpr f32 kRowHeight = 18.0f;

    bool m_initialized = false;
    bool m_visible = false;
    bool m_pending_defaults = true;

    f32 m_panel_x = 0.0f;
    f32 m_panel_y = 0.0f;

    enum class TargetMode : u8
    {
        SelectedObject,
        SurveyScan,
    };

    TargetMode m_target_mode = TargetMode::SelectedObject;

    std::optional<u64> m_open_target_override;
    u64 m_current_selected_id = 0;
    std::string m_current_selected_name;

    std::vector<std::string> m_techniques;
    i32 m_technique_index = 0;

    f32 m_duration_hours = 1.0f;
    f32 m_survey_center_ra_deg = 0.0f;
    f32 m_survey_center_dec_deg = 0.0f;
    f32 m_survey_radius_deg = 1.0f;

    InstrumentPanelCallbacks m_callbacks;

    std::unique_ptr<Button> m_btn_target_selected;
    std::unique_ptr<Button> m_btn_target_survey;
    std::unique_ptr<Button> m_btn_technique_cycle;
    std::unique_ptr<Button> m_btn_schedule;
    std::unique_ptr<Button> m_btn_cancel;

    std::unique_ptr<Slider> m_slider_duration;
    std::unique_ptr<Slider> m_slider_ra_deg;
    std::unique_ptr<Slider> m_slider_dec_deg;
    std::unique_ptr<Slider> m_slider_radius_deg;
};

} // namespace parallax::ui
