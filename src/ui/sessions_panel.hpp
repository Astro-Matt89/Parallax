#pragma once

/// @file sessions_panel.hpp
/// @brief Observation sessions panel (Sprint 08 Task 8.10).

#include "core/types.hpp"
#include "observation/observation_session.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace parallax::ui
{

struct SessionsPanelCallbacks
{
    std::function<void(u64)> abort_session;
};

struct SessionsPanelEntry
{
    u64 session_id = 0;
    std::string target_name;
    std::string technique;
    f32 completion_fraction = 0.0f;
    f32 elapsed_hours = 0.0f;
    f32 accumulated_snr = 0.0f;
    f32 expected_snr = 0.0f;
};

struct SessionsPanelCompletedEntry
{
    u64 session_id = 0;
    std::string target_name;
    std::string technique;
    f32 final_snr = 0.0f;
};

class SessionsPanel
{
public:
    SessionsPanel() = default;
    ~SessionsPanel() = default;

    SessionsPanel(const SessionsPanel&) = delete;
    SessionsPanel& operator=(const SessionsPanel&) = delete;
    SessionsPanel(SessionsPanel&&) = delete;
    SessionsPanel& operator=(SessionsPanel&&) = delete;

    void init(const SessionsPanelCallbacks& callbacks);

    void set_visible(bool visible);
    [[nodiscard]] bool is_visible() const;
    [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

    void update(std::vector<SessionsPanelEntry> active_entries,
                std::vector<SessionsPanelCompletedEntry> completed_entries,
                Vec2f mouse_pos, bool mouse_clicked, f32 dt,
                u32 viewport_width, u32 viewport_height);

    void render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const;

private:
    void layout_widgets(u32 viewport_width, u32 viewport_height);

    static constexpr f32 kPanelWidth = 430.0f;
    static constexpr f32 kPanelHeight = 420.0f;
    static constexpr f32 kPadding = 10.0f;
    static constexpr f32 kRowHeight = 18.0f;

    bool m_initialized = false;
    bool m_visible = false;

    f32 m_panel_x = 0.0f;
    f32 m_panel_y = 0.0f;

    SessionsPanelCallbacks m_callbacks;

    std::vector<SessionsPanelEntry> m_active_entries;
    std::vector<SessionsPanelCompletedEntry> m_completed_entries;
    std::vector<std::unique_ptr<Button>> m_abort_buttons;
};

} // namespace parallax::ui
