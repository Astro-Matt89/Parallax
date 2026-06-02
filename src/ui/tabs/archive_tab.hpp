#pragma once

#include "core/types.hpp"
#include "observation/data_archive.hpp"
#include "ui/shell/tab_id.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace parallax::rendering {
class LineRenderer;
}

namespace parallax::ui {
class BitmapFont;
}

namespace parallax::vulkan {
class Swapchain;
}

namespace parallax::ui::tabs {
class ArchiveTab final : public shell::TabContent {
public:
  using RecordId = std::uint64_t;

  ArchiveTab(BitmapFont &font, rendering::LineRenderer &line_renderer,
             vulkan::Swapchain &swapchain, observation::DataArchive &archive);
  ~ArchiveTab() override = default;

  ArchiveTab(const ArchiveTab &) = delete;
  ArchiveTab &operator=(const ArchiveTab &) = delete;
  ArchiveTab(ArchiveTab &&) = delete;
  ArchiveTab &operator=(ArchiveTab &&) = delete;

  void update(f64 delta_time) override;
  void render(VkCommandBuffer cmd, const shell::ViewportRect &rect) override;
  void on_input(const shell::InputEvent &event,
                const shell::ViewportRect &rect) override;

  [[nodiscard]] shell::TabId get_id() const override;

private:
  enum class FilterKind : u8 { All, Photometry, Spectroscopy, Imaging, Survey };

  enum class SortKind : u8 { Date, Target, SNR, Type };

  enum class SortDir : u8 { Asc, Desc };

  void refresh_cached_rows();
  void clamp_scroll(i32 visible_row_count);

  [[nodiscard]] bool selected_in_filtered_set() const;
  [[nodiscard]] bool passes_filter(const observation::DataRecord &record) const;

  BitmapFont &m_font;
  rendering::LineRenderer &m_line_renderer;
  vulkan::Swapchain &m_swapchain;
  observation::DataArchive &m_archive;

  FilterKind m_filter = FilterKind::All;
  SortKind m_sort_key = SortKind::Date;
  SortDir m_sort_dir = SortDir::Desc;

  std::optional<RecordId> m_selected;
  i32 m_scroll_row = 0;
  i32 m_hovered_row = -1;

  std::vector<RecordId> m_cached_ids;
  i32 m_last_visible_rows = 0;
};
} // namespace parallax::ui::tabs
