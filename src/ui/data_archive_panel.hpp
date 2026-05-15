#pragma once

/// @file data_archive_panel.hpp
/// @brief Data archive panel (Sprint 08 Task 8.10).

#include "core/types.hpp"
#include "observation/data_record.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"

#include <optional>
#include <string>
#include <vector>

namespace parallax::ui
{

struct DataArchivePanelRow
{
    std::size_t index = 0;
    const observation::DataRecord* record = nullptr;
    std::string target_name;
};

class DataArchivePanel
{
public:
    DataArchivePanel() = default;
    ~DataArchivePanel() = default;

    DataArchivePanel(const DataArchivePanel&) = delete;
    DataArchivePanel& operator=(const DataArchivePanel&) = delete;
    DataArchivePanel(DataArchivePanel&&) = delete;
    DataArchivePanel& operator=(DataArchivePanel&&) = delete;

    void init();
    void set_visible(bool visible);

    [[nodiscard]] bool is_visible() const;
    [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

    void update(std::vector<DataArchivePanelRow> rows,
                Vec2f mouse_pos, bool mouse_clicked,
                u32 viewport_width, u32 viewport_height);

    void render(BitmapFont& font, rendering::LineRenderer& lines, VkExtent2D extent) const;

private:
    [[nodiscard]] static std::string data_type_to_text(observation::DataType type);

    static constexpr f32 kPanelWidth = 760.0f;
    static constexpr f32 kPanelHeight = 240.0f;
    static constexpr f32 kPadding = 10.0f;
    static constexpr f32 kRowHeight = 18.0f;

    bool m_initialized = false;
    bool m_visible = false;

    f32 m_panel_x = 0.0f;
    f32 m_panel_y = 0.0f;

    std::vector<DataArchivePanelRow> m_rows;
    std::optional<std::size_t> m_selected_index;
};

} // namespace parallax::ui
