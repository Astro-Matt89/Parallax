/// @file data_archive_panel.cpp
/// @brief Data archive panel implementation.

#include "ui/data_archive_panel.hpp"
#include "ui/widgets.hpp"  // widget_colors::* (kBorder, kTextBright, kTextDim, ...)

#include <algorithm>
#include <format>

namespace parallax::ui
{

void DataArchivePanel::init()
{
    m_initialized = true;
}

void DataArchivePanel::set_visible(bool visible)
{
    m_visible = visible;
}

bool DataArchivePanel::is_visible() const
{
    return m_visible;
}

bool DataArchivePanel::is_mouse_over(Vec2f mouse_pos) const
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

void DataArchivePanel::update(std::vector<DataArchivePanelRow> rows,
                              Vec2f mouse_pos, bool mouse_clicked,
                              u32 viewport_width, u32 viewport_height)
{
    if (!m_initialized || !m_visible)
    {
        return;
    }

    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);
    m_panel_x = (vw - kPanelWidth) * 0.5f;
    m_panel_y = vh - kPanelHeight - 48.0f;

    m_rows = std::move(rows);

    if (mouse_clicked)
    {
        const f32 row_x = m_panel_x + kPadding;
        f32 row_y = m_panel_y + kPadding + kRowHeight * 2.0f;
        for (std::size_t i = 0; i < m_rows.size(); ++i)
        {
            const bool in_row = mouse_pos.x >= row_x
                && mouse_pos.x <= row_x + (kPanelWidth - 2.0f * kPadding)
                && mouse_pos.y >= row_y
                && mouse_pos.y <= row_y + kRowHeight;

            if (in_row)
            {
                m_selected_index = i;
                break;
            }
            row_y += kRowHeight;
        }
    }

    if (m_selected_index.has_value() && m_selected_index.value() >= m_rows.size())
    {
        m_selected_index.reset();
    }
}

std::string DataArchivePanel::data_type_to_text(observation::DataType type)
{
    switch (type)
    {
        case observation::DataType::PhotometricMeasurement: return "PhotometricMeasurement";
        case observation::DataType::LightCurve:             return "LightCurve";
        case observation::DataType::Spectrum:               return "Spectrum";
        case observation::DataType::Image:                  return "Image";
        case observation::DataType::SurveySourceList:       return "SurveySourceList";
        case observation::DataType::Mock:                   return "Mock";
        default:                                            return "Unknown";
    }
}

void DataArchivePanel::render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const
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
    font.draw_text("=== DATA ARCHIVE ===", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;
    font.draw_text("JD        TARGET                TECHNIQUE      SNR     TYPE", cx, cy, 1.0f, widget_colors::kTextDim);
    cy += kRowHeight;

    if (m_rows.empty())
    {
        font.draw_text("(no records)", cx, cy, 1.0f, widget_colors::kTextDim);
        return;
    }

    for (std::size_t i = 0; i < m_rows.size(); ++i)
    {
        const auto& row = m_rows[i];
        if (row.record == nullptr)
        {
            continue;
        }

        const std::string line = std::format("{:.4f}  {:<20} {:<13} {:>6.1f}  {}",
                                             row.record->observation_jd,
                                             row.target_name,
                                             row.record->technique,
                                             row.record->achieved_snr,
                                             data_type_to_text(row.record->type));
        const Vec3f color = (m_selected_index.has_value() && m_selected_index.value() == i)
            ? widget_colors::kTextBright
            : widget_colors::kTextDim;
        font.draw_text(line, cx, cy, 1.0f, color);
        cy += kRowHeight;
    }

    if (!m_selected_index.has_value())
    {
        return;
    }

    const auto& selected = m_rows[m_selected_index.value()];
    if (selected.record == nullptr)
    {
        return;
    }

    cy += 4.0f;
    font.draw_text("--- DETAILS ---", cx, cy, 1.0f, widget_colors::kTextBright);
    cy += kRowHeight;

    if (selected.record->measurements.empty() && selected.record->uncertainties.empty())
    {
        font.draw_text("(no measurement fields)", cx, cy, 1.0f, widget_colors::kTextDim);
        return;
    }

    for (const auto& [key, value] : selected.record->measurements)
    {
        const std::string line = std::format("M {} -> {:.6g}", key, value);
        font.draw_text(line, cx, cy, 1.0f, widget_colors::kTextBright);
        cy += kRowHeight;
    }
    for (const auto& [key, value] : selected.record->uncertainties)
    {
        const std::string line = std::format("U {} -> {:.6g}", key, static_cast<double>(value));
        font.draw_text(line, cx, cy, 1.0f, widget_colors::kTextDim);
        cy += kRowHeight;
    }
}

} // namespace parallax::ui
