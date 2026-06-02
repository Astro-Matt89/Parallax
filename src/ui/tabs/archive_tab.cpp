#include "ui/tabs/archive_tab.hpp"

#include "core/logger.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/widgets.hpp"
#include "vulkan/swapchain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace parallax::ui::tabs {
namespace {
constexpr u32 kPaddingX = 8;
constexpr u32 kHeaderH = 24;
constexpr u32 kFilterRowH = 22;
constexpr u32 kSortRowH = 22;
constexpr u32 kRowGap = 4;
constexpr u32 kTableRowH = 18;
constexpr u32 kDetailsRatioPct = 30;
constexpr u32 kActionRowH = 26;

constexpr f32 kGlyphW = 8.0f;

// TODO: deduplicate shell colour palette.
constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
constexpr Vec3f kHeaderColor = widget_colors::kTextBright;
constexpr Vec3f kDimColor = widget_colors::kTextDim;
constexpr Vec4f kHoverColor = widget_colors::kHighlight;
constexpr Vec4f kSelectedColor{0.0f, 0.35f, 0.0f, 0.45f};
constexpr Vec4f kAltRowColor{0.0f, 0.18f, 0.0f, 0.20f};
constexpr Vec4f kButtonInactive{0.0f, 0.20f, 0.0f, 0.45f};
constexpr Vec4f kButtonDisabled{0.0f, 0.10f, 0.0f, 0.35f};

struct ArchiveLayout {
  shell::ViewportRect header_rect{};
  shell::ViewportRect filter_row_rect{};
  shell::ViewportRect sort_row_rect{};
  shell::ViewportRect action_row_rect{};
  shell::ViewportRect table_rect{};
  shell::ViewportRect table_header_rect{};
  shell::ViewportRect table_body_rect{};
  shell::ViewportRect details_rect{};

  std::array<shell::ViewportRect, 5> filter_buttons{};
  std::array<shell::ViewportRect, 4> sort_buttons{};
  std::array<shell::ViewportRect, 3> action_buttons{};
};

[[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport) {
  return {(px.x / viewport.x) * 2.0f - 1.0f, (px.y / viewport.y) * 2.0f - 1.0f};
}

void draw_filled_rect(rendering::LineRenderer &lines,
                      const shell::ViewportRect &rect, const Vec4f color,
                      const Vec2f viewport) {
  if (!rect.is_valid()) {
    return;
  }

  const f32 x0 = static_cast<f32>(rect.x);
  const f32 x1 = static_cast<f32>(rect.right());
  for (u32 y = rect.y; y < rect.bottom(); ++y) {
    const f32 py = static_cast<f32>(y) + 0.5f;
    lines.add_line(pixel_to_ndc({x0, py}, viewport),
                   pixel_to_ndc({x1, py}, viewport), color);
  }
}

[[nodiscard]] shell::ViewportRect make_rect(i32 x, i32 y, i32 width,
                                            i32 height) {
  return {static_cast<u32>(std::max(0, x)), static_cast<u32>(std::max(0, y)),
          static_cast<u32>(std::max(0, width)),
          static_cast<u32>(std::max(0, height))};
}

[[nodiscard]] ArchiveLayout build_layout(const shell::ViewportRect &rect) {
  ArchiveLayout layout{};

  const i32 content_x = static_cast<i32>(rect.x + kPaddingX);
  const i32 content_w =
      static_cast<i32>(rect.width) - static_cast<i32>(2 * kPaddingX);
  i32 cursor_y = static_cast<i32>(rect.y + kPaddingX);

  layout.header_rect =
      make_rect(content_x, cursor_y, content_w, static_cast<i32>(kHeaderH));
  cursor_y += static_cast<i32>(kHeaderH + kRowGap);

  layout.filter_row_rect =
      make_rect(content_x, cursor_y, content_w, static_cast<i32>(kFilterRowH));
  const i32 filter_w = content_w / 5;
  for (i32 i = 0; i < 5; ++i) {
    const i32 x = content_x + i * filter_w;
    const i32 w = (i == 4) ? (content_w - i * filter_w) : filter_w;
    layout.filter_buttons[static_cast<std::size_t>(i)] =
        make_rect(x, cursor_y, w, static_cast<i32>(kFilterRowH));
  }
  cursor_y += static_cast<i32>(kFilterRowH + kRowGap);

  layout.sort_row_rect =
      make_rect(content_x, cursor_y, content_w, static_cast<i32>(kSortRowH));
  const i32 sort_w = content_w / 4;
  for (i32 i = 0; i < 4; ++i) {
    const i32 x = content_x + i * sort_w;
    const i32 w = (i == 3) ? (content_w - i * sort_w) : sort_w;
    layout.sort_buttons[static_cast<std::size_t>(i)] =
        make_rect(x, cursor_y, w, static_cast<i32>(kSortRowH));
  }
  cursor_y += static_cast<i32>(kSortRowH + kRowGap);

  layout.action_row_rect =
      make_rect(content_x, cursor_y, content_w, static_cast<i32>(kActionRowH));
  const i32 action_w = content_w / 3;
  for (i32 i = 0; i < 3; ++i) {
    const i32 x = content_x + i * action_w;
    const i32 w = (i == 2) ? (content_w - i * action_w) : action_w;
    layout.action_buttons[static_cast<std::size_t>(i)] =
        make_rect(x, cursor_y, w, static_cast<i32>(kActionRowH));
  }
  cursor_y += static_cast<i32>(kActionRowH + kRowGap);

  const i32 available_h =
      static_cast<i32>(rect.bottom()) - cursor_y - static_cast<i32>(kPaddingX);
  const i32 details_h =
      std::max(static_cast<i32>(kTableRowH * 5),
               (available_h * static_cast<i32>(kDetailsRatioPct)) / 100);
  const i32 details_y =
      static_cast<i32>(rect.bottom()) - static_cast<i32>(kPaddingX) - details_h;

  layout.details_rect = make_rect(content_x, details_y, content_w, details_h);
  layout.table_rect =
      make_rect(content_x, cursor_y, content_w,
                std::max(0, details_y - cursor_y - static_cast<i32>(kRowGap)));

  layout.table_header_rect = make_rect(
      static_cast<i32>(layout.table_rect.x),
      static_cast<i32>(layout.table_rect.y),
      static_cast<i32>(layout.table_rect.width), static_cast<i32>(kTableRowH));

  const i32 body_y = static_cast<i32>(layout.table_rect.y + kTableRowH);
  const i32 body_h =
      static_cast<i32>(layout.table_rect.height) - static_cast<i32>(kTableRowH);
  layout.table_body_rect =
      make_rect(static_cast<i32>(layout.table_rect.x), body_y,
                static_cast<i32>(layout.table_rect.width), std::max(0, body_h));

  return layout;
}

[[nodiscard]] const char *filter_label(const i32 filter_index) {
  switch (filter_index) {
  case 0:
    return "All";
  case 1:
    return "Photometry";
  case 2:
    return "Spectroscopy";
  case 3:
    return "Imaging";
  case 4:
    return "Survey";
  }
  return "All";
}

[[nodiscard]] const char *sort_label(const i32 sort_index) {
  switch (sort_index) {
  case 0:
    return "Date";
  case 1:
    return "Target";
  case 2:
    return "SNR";
  case 3:
    return "Type";
  }
  return "Date";
}

[[nodiscard]] const char *data_type_to_text(const observation::DataType type) {
  switch (type) {
  case observation::DataType::PhotometricMeasurement:
    return "Photometry";
  case observation::DataType::LightCurve:
    return "Photometry";
  case observation::DataType::Spectrum:
    return "Spectroscopy";
  case observation::DataType::Image:
    return "Imaging";
  case observation::DataType::SurveySourceList:
    return "Survey";
  case observation::DataType::Mock:
    return "Mock";
  }
  return "Unknown";
}

[[nodiscard]] std::string format_target(const observation::DataRecord &record) {
  if (record.target_object_id == 0) {
    return "Survey";
  }

  return std::format("OBJ {}", record.target_object_id);
}

[[nodiscard]] std::string
format_size(const std::vector<std::uint8_t> &raw_data) {
  if (raw_data.empty()) {
    return "0 B";
  }

  const f64 bytes = static_cast<f64>(raw_data.size());
  if (bytes < 1024.0) {
    return std::format("{:.0f} B", bytes);
  }

  return std::format("{:.1f} KB", bytes / 1024.0);
}

void apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect &rect) {
  VkViewport vk_viewport{};
  vk_viewport.x = static_cast<f32>(rect.x);
  vk_viewport.y = static_cast<f32>(rect.y);
  vk_viewport.width = static_cast<f32>(rect.width);
  vk_viewport.height = static_cast<f32>(rect.height);
  vk_viewport.minDepth = 0.0f;
  vk_viewport.maxDepth = 1.0f;
  vkCmdSetViewport(cmd, 0, 1, &vk_viewport);

  VkRect2D scissor{};
  scissor.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
  scissor.extent = {rect.width, rect.height};
  vkCmdSetScissor(cmd, 0, 1, &scissor);
}
} // namespace

ArchiveTab::ArchiveTab(BitmapFont &font, rendering::LineRenderer &line_renderer,
                       vulkan::Swapchain &swapchain,
                       observation::DataArchive &archive)
    : m_font(font), m_line_renderer(line_renderer), m_swapchain(swapchain),
      m_archive(archive) {}

void ArchiveTab::update(f64 delta_time) {
  static_cast<void>(delta_time);
  refresh_cached_rows();
}

void ArchiveTab::render(VkCommandBuffer cmd, const shell::ViewportRect &rect) {
  if (!rect.is_valid()) {
    return;
  }

  apply_viewport(cmd, rect);

  const ArchiveLayout layout = build_layout(rect);
  const VkExtent2D extent = m_swapchain.get_extent();
  const Vec2f viewport{static_cast<f32>(extent.width),
                       static_cast<f32>(extent.height)};

  m_line_renderer.begin_frame();

  draw_filled_rect(m_line_renderer, rect, kBackgroundColor, viewport);

  const std::string header = std::format("({} records, {} filtered)",
                                         m_archive.size(), m_cached_ids.size());
  m_font.draw_text("DATA ARCHIVE", static_cast<f32>(layout.header_rect.x),
                   static_cast<f32>(layout.header_rect.y + 4), 1.0f,
                   kHeaderColor);
  m_font.draw_text(header,
                   static_cast<f32>(layout.header_rect.x + 14 * kGlyphW),
                   static_cast<f32>(layout.header_rect.y + 4), 1.0f, kDimColor);

  for (i32 i = 0; i < 5; ++i) {
    const auto filter = static_cast<FilterKind>(i);
    const bool active = (m_filter == filter);
    const shell::ViewportRect btn =
        layout.filter_buttons[static_cast<std::size_t>(i)];
    draw_filled_rect(m_line_renderer, btn,
                     active ? kSelectedColor : kButtonInactive, viewport);
    m_font.draw_text(filter_label(i), static_cast<f32>(btn.x + 4),
                     static_cast<f32>(btn.y + 3), 1.0f,
                     active ? kHeaderColor : kDimColor);
  }

  // TODO(Sprint 10+): use Unicode arrows when bitmap font supports glyphs.
  const char *dir_text = (m_sort_dir == SortDir::Asc) ? "^" : "v";
  for (i32 i = 0; i < 4; ++i) {
    const auto sort = static_cast<SortKind>(i);
    const bool active = (m_sort_key == sort);
    const shell::ViewportRect btn =
        layout.sort_buttons[static_cast<std::size_t>(i)];
    draw_filled_rect(m_line_renderer, btn,
                     active ? kSelectedColor : kButtonInactive, viewport);

    std::string text = sort_label(i);
    if (active) {
      text += " ";
      text += dir_text;
    }

    m_font.draw_text(text, static_cast<f32>(btn.x + 4),
                     static_cast<f32>(btn.y + 3), 1.0f,
                     active ? kHeaderColor : kDimColor);
  }

  const bool has_selection = m_selected.has_value();
  constexpr std::array<const char *, 3> kActions{"EXPORT", "DELETE", "ANALYZE"};
  for (i32 i = 0; i < 3; ++i) {
    const shell::ViewportRect btn =
        layout.action_buttons[static_cast<std::size_t>(i)];
    draw_filled_rect(m_line_renderer, btn,
                     has_selection ? kSelectedColor : kButtonDisabled,
                     viewport);
    m_font.draw_text(kActions[static_cast<std::size_t>(i)],
                     static_cast<f32>(btn.x + 4), static_cast<f32>(btn.y + 5),
                     1.0f, has_selection ? kHeaderColor : kDimColor);
  }

  draw_filled_rect(m_line_renderer, layout.table_header_rect, kButtonInactive,
                   viewport);
  m_font.draw_text("JD           Target          Technique     SNR      Size",
                   static_cast<f32>(layout.table_header_rect.x + 4),
                   static_cast<f32>(layout.table_header_rect.y + 2), 1.0f,
                   kDimColor);

  const i32 visible_rows =
      static_cast<i32>(layout.table_body_rect.height / kTableRowH);
  i32 y = static_cast<i32>(layout.table_body_rect.y);
  for (i32 local = 0; local < visible_rows; ++local) {
    const i32 absolute_row = m_scroll_row + local;
    if (absolute_row >= static_cast<i32>(m_cached_ids.size())) {
      break;
    }

    const shell::ViewportRect row_rect =
        make_rect(static_cast<i32>(layout.table_body_rect.x), y,
                  static_cast<i32>(layout.table_body_rect.width),
                  static_cast<i32>(kTableRowH));

    if ((absolute_row % 2) == 1) {
      draw_filled_rect(m_line_renderer, row_rect, kAltRowColor, viewport);
    }

    const RecordId id = m_cached_ids[static_cast<std::size_t>(absolute_row)];
    const observation::DataRecord *record = m_archive.get_by_id(id);
    if (record != nullptr) {
      if (m_selected.has_value() && m_selected.value() == id) {
        draw_filled_rect(m_line_renderer, row_rect, kSelectedColor, viewport);
      } else if (m_hovered_row == absolute_row) {
        draw_filled_rect(m_line_renderer, row_rect, kHoverColor, viewport);
      }

      const std::string row_text = std::format(
          "{:<12.4f} {:<15} {:<12} {:>7.1f}  {:>7}", record->observation_jd,
          format_target(*record), record->technique, record->achieved_snr,
          format_size(record->raw_data));
      m_font.draw_text(row_text, static_cast<f32>(row_rect.x + 4),
                       static_cast<f32>(row_rect.y + 1), 1.0f, kHeaderColor);
    }

    y += static_cast<i32>(kTableRowH);
  }

  draw_filled_rect(m_line_renderer, layout.details_rect, kButtonInactive,
                   viewport);

  f32 details_y = static_cast<f32>(layout.details_rect.y + 3);
  const auto draw_detail = [&](const std::string &text, const Vec3f color,
                               f32 *y_pos) {
    if (*y_pos + kTableRowH > static_cast<f32>(layout.details_rect.bottom())) {
      return;
    }
    m_font.draw_text(text, static_cast<f32>(layout.details_rect.x + 4), *y_pos,
                     1.0f, color);
    *y_pos += static_cast<f32>(kTableRowH);
  };

  if (!m_selected.has_value()) {
    draw_detail("Select a record above", kDimColor, &details_y);
  } else {
    const observation::DataRecord *record =
        m_archive.get_by_id(m_selected.value());
    if (record == nullptr) {
      draw_detail("Select a record above", kDimColor, &details_y);
    } else {
      draw_detail(
          std::format("ID: {}   Session: {}", record->id, record->session_id),
          kHeaderColor, &details_y);
      draw_detail(std::format("Target: {}", format_target(*record)),
                  kHeaderColor, &details_y);
      draw_detail(std::format("Type: {}   Technique: {}",
                              data_type_to_text(record->type),
                              record->technique),
                  kHeaderColor, &details_y);
      draw_detail(std::format("JD: {:.6f}", record->observation_jd),
                  kHeaderColor, &details_y);
      draw_detail(std::format("Duration: {:.2f} h   SNR: {:.2f}",
                              record->duration_hours, record->achieved_snr),
                  kHeaderColor, &details_y);

      draw_detail("Measurements:", kDimColor, &details_y);
      for (const auto &[key, value] : record->measurements) {
        draw_detail(std::format("M {} = {:.6g}", key, value), kHeaderColor,
                    &details_y);
      }
      for (const auto &[key, value] : record->uncertainties) {
        draw_detail(
            std::format("U {} = {:.6g}", key, static_cast<double>(value)),
            kDimColor, &details_y);
      }
    }
  }

  m_line_renderer.render(cmd);
  m_font.render(cmd, extent);

  m_last_visible_rows = visible_rows;
}

void ArchiveTab::on_input(const shell::InputEvent &event,
                          const shell::ViewportRect &rect) {
  if (!rect.contains(static_cast<i32>(event.mouse_pos.x),
                     static_cast<i32>(event.mouse_pos.y))) {
    m_hovered_row = -1;
    return;
  }

  const ArchiveLayout layout = build_layout(rect);
  const i32 visible_rows =
      static_cast<i32>(layout.table_body_rect.height / kTableRowH);

  m_hovered_row = -1;
  if (layout.table_body_rect.contains(static_cast<i32>(event.mouse_pos.x),
                                      static_cast<i32>(event.mouse_pos.y))) {
    const i32 local = (static_cast<i32>(event.mouse_pos.y) -
                       static_cast<i32>(layout.table_body_rect.y)) /
                      static_cast<i32>(kTableRowH);
    const i32 absolute = m_scroll_row + local;
    if (absolute >= 0 && absolute < static_cast<i32>(m_cached_ids.size())) {
      m_hovered_row = absolute;
    }
  }

  if (event.scroll_delta != 0.0f &&
      layout.table_body_rect.contains(static_cast<i32>(event.mouse_pos.x),
                                      static_cast<i32>(event.mouse_pos.y))) {
    i32 delta = static_cast<i32>(std::round(event.scroll_delta));
    if (delta == 0) {
      delta = (event.scroll_delta > 0.0f) ? 1 : -1;
    }
    m_scroll_row -= delta;
    clamp_scroll(visible_rows);
  }

  if (!event.was_click || event.click_button != shell::MouseButton::Left) {
    return;
  }

  for (i32 i = 0; i < 5; ++i) {
    if (layout.filter_buttons[static_cast<std::size_t>(i)].contains(
            event.mouse_pos)) {
      m_filter = static_cast<FilterKind>(i);
      refresh_cached_rows();
      return;
    }
  }

  for (i32 i = 0; i < 4; ++i) {
    if (layout.sort_buttons[static_cast<std::size_t>(i)].contains(
            event.mouse_pos)) {
      const SortKind clicked = static_cast<SortKind>(i);
      if (clicked == m_sort_key) {
        m_sort_dir =
            (m_sort_dir == SortDir::Asc) ? SortDir::Desc : SortDir::Asc;
      } else {
        m_sort_key = clicked;
        m_sort_dir = (clicked == SortKind::Date || clicked == SortKind::SNR)
                         ? SortDir::Desc
                         : SortDir::Asc;
      }
      refresh_cached_rows();
      return;
    }
  }

  if (layout.action_buttons[0].contains(event.mouse_pos)) {
    if (m_selected.has_value()) {
      PLX_CORE_INFO("ArchiveTab::TODO Export/Analyze in Sprint 10/11");
    }
    return;
  }

  if (layout.action_buttons[1].contains(event.mouse_pos)) {
    if (!m_selected.has_value()) {
      return;
    }

    const RecordId id = m_selected.value();
    if (m_archive.remove_record(id)) {
      PLX_CORE_INFO("ArchiveTab: deleted record {}", id);
      m_selected.reset();
      refresh_cached_rows();
    } else {
      PLX_CORE_WARN("ArchiveTab: failed to delete missing record {}", id);
    }
    return;
  }

  if (layout.action_buttons[2].contains(event.mouse_pos)) {
    if (m_selected.has_value()) {
      PLX_CORE_INFO("ArchiveTab::TODO Export/Analyze in Sprint 10/11");
    }
    return;
  }

  if (m_hovered_row >= 0 &&
      m_hovered_row < static_cast<i32>(m_cached_ids.size())) {
    m_selected = m_cached_ids[static_cast<std::size_t>(m_hovered_row)];
  }
}

shell::TabId ArchiveTab::get_id() const { return shell::TabId::Archive; }

void ArchiveTab::refresh_cached_rows() {
  std::vector<const observation::DataRecord *> records = m_archive.get_all();

  records.erase(std::remove_if(records.begin(), records.end(),
                               [this](const observation::DataRecord *record) {
                                 return record == nullptr ||
                                        !passes_filter(*record);
                               }),
                records.end());

  std::sort(records.begin(), records.end(),
            [this](const observation::DataRecord *lhs,
                   const observation::DataRecord *rhs) {
              auto cmp = [lhs, rhs](const auto &left, const auto &right) {
                if (left < right) {
                  return -1;
                }
                if (left > right) {
                  return 1;
                }
                return 0;
              };

              i32 order = 0;
              switch (m_sort_key) {
              case SortKind::Date:
                order = cmp(lhs->observation_jd, rhs->observation_jd);
                break;
              case SortKind::Target:
                order = cmp(lhs->target_object_id, rhs->target_object_id);
                break;
              case SortKind::SNR:
                order = cmp(lhs->achieved_snr, rhs->achieved_snr);
                break;
              case SortKind::Type:
                order = cmp(static_cast<i32>(lhs->type),
                            static_cast<i32>(rhs->type));
                break;
              }

              if (order == 0) {
                order = cmp(lhs->id, rhs->id);
              }

              if (m_sort_dir == SortDir::Desc) {
                order = -order;
              }

              return order < 0;
            });

  m_cached_ids.clear();
  m_cached_ids.reserve(records.size());
  for (const observation::DataRecord *record : records) {
    m_cached_ids.push_back(record->id);
  }

  if (!selected_in_filtered_set() && m_selected.has_value()) {
    m_selected.reset();
    PLX_CORE_INFO("ArchiveTab: selection cleared (record no longer in view)");
  }

  clamp_scroll(std::max(1, m_last_visible_rows));
}

void ArchiveTab::clamp_scroll(const i32 visible_row_count) {
  const i32 max_scroll = std::max(0, static_cast<i32>(m_cached_ids.size()) -
                                         std::max(visible_row_count, 1));
  m_scroll_row = std::clamp(m_scroll_row, 0, max_scroll);
}

bool ArchiveTab::selected_in_filtered_set() const {
  if (!m_selected.has_value()) {
    return true;
  }

  return std::find(m_cached_ids.begin(), m_cached_ids.end(),
                   m_selected.value()) != m_cached_ids.end();
}

bool ArchiveTab::passes_filter(const observation::DataRecord &record) const {
  switch (m_filter) {
  case FilterKind::All:
    return true;
  case FilterKind::Photometry:
    return record.type == observation::DataType::PhotometricMeasurement ||
           record.type == observation::DataType::LightCurve;
  case FilterKind::Spectroscopy:
    return record.type == observation::DataType::Spectrum;
  case FilterKind::Imaging:
    return record.type == observation::DataType::Image;
  case FilterKind::Survey:
    return record.type == observation::DataType::SurveySourceList;
  }

  return true;
}
} // namespace parallax::ui::tabs
