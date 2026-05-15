/// @file sessions_panel.cpp
/// @brief Observation sessions panel implementation.

#include "ui/sessions_panel.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace parallax::ui
{

void SessionsPanel::init(const SessionsPanelCallbacks& callbacks)
{
    m_callbacks = callbacks;
    m_initialized = true;
}

void SessionsPanel::set_visible(bool visible)
{
    m_visible = visible;
}

bool SessionsPanel::is_visible() const
{
    return m_visible;
}

bool SessionsPanel::is_mouse_over(Vec2f mouse_pos) const
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

void SessionsPanel::layout_widgets(u32 viewport_width, u32 viewport_height)
{
    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);
    m_panel_x = vw - kPanelWidth - 10.0f;
    m_panel_y = (vh - kPanelHeight) * 0.5f;
}

void SessionsPanel::update(std::vector<SessionsPanelEntry> active_entries,
                           std::vector<SessionsPanelCompletedEntry> completed_entries,
                           Vec2f mouse_pos, bool mouse_clicked, f32 dt,
                           u32 viewport_width, u32 viewport_height)
{
    if (!m_initialized || !m_visible)
    {
        return;
    }

    layout_widgets(viewport_width, viewport_height);

    m_active_entries = std::move(active_entries);
    m_completed_entries = std::move(completed_entries);

    m_abort_buttons.clear();
    m_abort_buttons.reserve(m_active_entries.size());

    f32 cy = m_panel_y + kPadding + (kRowHeight * 2.0f);
    for (const auto& entry : m_active_entries)
    {
        auto btn = std::make_unique<Button>(
            "ABORT", Vec2f{m_panel_x + kPanelWidth - 100.0f, cy + kRowHeight * 2.0f}, Vec2f{80.0f, 24.0f},
            [this, id = entry.session_id]()
            {
                if (m_callbacks.abort_session)
                {
                    m_callbacks.abort_session(id);
                }
            });
        btn->update(mouse_pos, mouse_clicked, dt);
        m_abort_buttons.push_back(std::move(btn));
        cy += kRowHeight * 4.0f;
    }
}

void SessionsPanel::render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const
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
    font.draw_text("=== OBSERVATION SESSIONS ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;
    font.draw_text("ACTIVE", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    for (std::size_t i = 0; i < m_active_entries.size(); ++i)
    {
        const auto& entry = m_active_entries[i];
        font.draw_text("* " + entry.target_name, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;

        font.draw_text("  Magic Instrument / " + entry.technique, cx, cy, 1.0f, widget_colors::kTextDim);
        cy += kRowHeight;

        const f32 clamped = std::clamp(entry.completion_fraction, 0.0f, 1.0f);
        const i32 bars = static_cast<i32>(std::round(clamped * 10.0f));
        std::string bar = "[";
        for (i32 b = 0; b < 10; ++b)
        {
            bar += (b < bars) ? "#" : "-";
        }
        bar += "] ";
        bar += std::format("{:.0f}% ({:.1f}h)", clamped * 100.0f, entry.elapsed_hours);
        font.draw_text(bar, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;

        const std::string snr_line = std::format("  SNR {:.1f} / {:.1f}", entry.accumulated_snr, entry.expected_snr);
        font.draw_text(snr_line, cx, cy, 1.0f, widget_colors::kTextBright);

        m_abort_buttons[i]->render(font, lines, vp);
        cy += kRowHeight * 2.0f;
    }

    cy += 4.0f;
    font.draw_text("COMPLETED", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    for (const auto& entry : m_completed_entries)
    {
        // TODO(Task 8.11): Replace pending level placeholder with actual analyzer-derived level.
        const std::string line = std::format("✓ {}  {}  SNR {:.1f}  pending",
                                             entry.target_name, entry.technique, entry.final_snr);
        font.draw_text(line, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }
}

} // namespace parallax::ui
