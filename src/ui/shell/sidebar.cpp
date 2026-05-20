#include "ui/shell/sidebar.hpp"

#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"
#include "ui/widgets.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace parallax::ui::shell
{
    namespace
    {
        constexpr u32 kPaddingX = 8;
        constexpr u32 kSectionHeaderH = 16;
        constexpr u32 kRowHeight = 18;
        constexpr u32 kSectionGap = 8;
        constexpr u32 kBorderWidth = 1;

        constexpr f32 kGlyphW = 8.0f;

        constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec4f kBorderColor = widget_colors::kBorderBright;
        constexpr Vec3f kSectionHeaderColor = widget_colors::kTextDim;
        constexpr Vec3f kContentColor = widget_colors::kTextBright;
        constexpr Vec3f kInactiveColor{0.0f, 0.45f, 0.0f};
        constexpr Vec3f kStatusColor = widget_colors::kTextDim;
        constexpr Vec4f kHoverHighlight = widget_colors::kHighlight;
        constexpr Vec4f kSelectedHighlight{0.0f, 0.35f, 0.0f, 0.45f};

        [[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport)
        {
            return {
                (px.x / viewport.x) * 2.0f - 1.0f,
                (px.y / viewport.y) * 2.0f - 1.0f
            };
        }

        [[nodiscard]] std::string format_time_scale_label(const SidebarTimeState& time)
        {
            if (time.paused)
            {
                return "x0";
            }

            const f64 rounded = std::round(time.time_scale);
            if (std::abs(time.time_scale - rounded) < 0.0001)
            {
                return std::format("x{}", static_cast<i64>(rounded));
            }

            return std::format("x{:.1f}", time.time_scale);
        }

        void draw_filled_rect(rendering::LineRenderer& lines,
                              const ViewportRect& rect,
                              const Vec4f color,
                              const Vec2f viewport)
        {
            if (!rect.is_valid())
            {
                return;
            }

            const f32 x0 = static_cast<f32>(rect.x);
            const f32 x1 = static_cast<f32>(rect.right());

            for (u32 y = rect.y; y < rect.bottom(); ++y)
            {
                const f32 py = static_cast<f32>(y) + 0.5f;
                lines.add_line(
                    pixel_to_ndc({x0, py}, viewport),
                    pixel_to_ndc({x1, py}, viewport),
                    color);
            }
        }

        [[nodiscard]] ViewportRect make_rect(const i32 x, const i32 y, const i32 width, const i32 height)
        {
            return {
                static_cast<u32>(std::max(0, x)),
                static_cast<u32>(std::max(0, y)),
                static_cast<u32>(std::max(0, width)),
                static_cast<u32>(std::max(0, height))
            };
        }
    }

    Sidebar::Sidebar(BitmapFont& font)
        : m_font(font)
    {
    }

    void Sidebar::set_callbacks(SidebarCallbacks callbacks)
    {
        m_callbacks = std::move(callbacks);
    }

    void Sidebar::set_state(SidebarState state)
    {
        m_state = std::move(state);
    }

    ViewportRect Sidebar::compute_rect(const ViewportRect& available) noexcept
    {
        return ViewportRect{
            .x = available.x,
            .y = available.y,
            .width = kSidebarWidth,
            .height = available.height
        };
    }

    std::vector<Sidebar::InteractiveElement> Sidebar::build_interactive_elements(
        const ViewportRect& sidebar_rect) const
    {
        std::vector<InteractiveElement> elements;
        elements.reserve(m_state.instruments.size() + kTabIdCount + m_state.stats.size() + 4);

        const i32 content_x = static_cast<i32>(sidebar_rect.x + kPaddingX);
        const i32 content_w = static_cast<i32>(sidebar_rect.width) - static_cast<i32>(2 * kPaddingX + kBorderWidth);

        i32 cursor_y = static_cast<i32>(sidebar_rect.y);

        // INSTRUMENTS
        cursor_y += static_cast<i32>(kSectionHeaderH);
        for (i32 i = 0; i < static_cast<i32>(m_state.instruments.size()); ++i)
        {
            elements.push_back(InteractiveElement{
                .kind = ElementKind::Instrument,
                .rect = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kRowHeight)),
                .index = i
            });
            cursor_y += static_cast<i32>(kRowHeight);
        }

        cursor_y += static_cast<i32>(kSectionGap);

        // TABS
        cursor_y += static_cast<i32>(kSectionHeaderH);
        for (i32 i = 0; i < static_cast<i32>(kTabIdCount); ++i)
        {
            elements.push_back(InteractiveElement{
                .kind = ElementKind::Tab,
                .rect = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kRowHeight)),
                .index = i
            });
            cursor_y += static_cast<i32>(kRowHeight);
        }

        cursor_y += static_cast<i32>(kSectionGap);

        // TIME
        cursor_y += static_cast<i32>(kSectionHeaderH);

        const std::string pause_text = m_state.time.paused ? "[ > ]" : "[||]";
        const std::string scale_text = format_time_scale_label(m_state.time);

        const i32 piece_y = cursor_y;
        i32 piece_x = content_x;
        const i32 gap = 8;

        const i32 pause_w = static_cast<i32>(pause_text.size() * kGlyphW + 8.0f);
        elements.push_back(InteractiveElement{
            .kind = ElementKind::TimePause,
            .rect = make_rect(piece_x, piece_y, pause_w, static_cast<i32>(kRowHeight)),
            .index = -1
        });
        piece_x += pause_w + gap;

        const i32 minus_w = 16;
        elements.push_back(InteractiveElement{
            .kind = ElementKind::TimeDown,
            .rect = make_rect(piece_x, piece_y, minus_w, static_cast<i32>(kRowHeight)),
            .index = -1
        });
        piece_x += minus_w + gap;

        const i32 scale_w = static_cast<i32>(scale_text.size() * kGlyphW + 8.0f);
        elements.push_back(InteractiveElement{
            .kind = ElementKind::TimeScale,
            .rect = make_rect(piece_x, piece_y, scale_w, static_cast<i32>(kRowHeight)),
            .index = -1
        });
        piece_x += scale_w + gap;

        const i32 plus_w = 16;
        elements.push_back(InteractiveElement{
            .kind = ElementKind::TimeUp,
            .rect = make_rect(piece_x, piece_y, plus_w, static_cast<i32>(kRowHeight)),
            .index = -1
        });

        cursor_y += static_cast<i32>(kRowHeight);
        cursor_y += static_cast<i32>(kSectionGap);

        // STATS
        cursor_y += static_cast<i32>(kSectionHeaderH);
        for (i32 i = 0; i < static_cast<i32>(m_state.stats.size()); ++i)
        {
            elements.push_back(InteractiveElement{
                .kind = ElementKind::Stats,
                .rect = make_rect(content_x, cursor_y, content_w, static_cast<i32>(kRowHeight)),
                .index = i
            });
            cursor_y += static_cast<i32>(kRowHeight);
        }

        return elements;
    }

    bool Sidebar::contains(const ViewportRect& rect, const Vec2f& point) noexcept
    {
        return rect.contains(point);
    }

    bool Sidebar::handle_input(const InputEvent& event, const ViewportRect& sidebar_rect)
    {
        if (!event.inside_viewport || !contains(sidebar_rect, event.mouse_pos))
        {
            m_hovered_row = -1;
            return false;
        }

        const std::vector<InteractiveElement> elements = build_interactive_elements(sidebar_rect);

        m_hovered_row = -1;
        for (i32 i = 0; i < static_cast<i32>(elements.size()); ++i)
        {
            if (contains(elements[static_cast<std::size_t>(i)].rect, event.mouse_pos))
            {
                m_hovered_row = i;
                break;
            }
        }

        if (event.was_click)
        {
            if (event.click_button == MouseButton::Left && m_hovered_row >= 0)
            {
                const InteractiveElement& hit = elements[static_cast<std::size_t>(m_hovered_row)];

                switch (hit.kind)
                {
                    case ElementKind::Instrument:
                        if (m_callbacks.on_instrument_selected
                            && hit.index >= 0
                            && hit.index < static_cast<i32>(m_state.instruments.size()))
                        {
                            m_callbacks.on_instrument_selected(
                                m_state.instruments[static_cast<std::size_t>(hit.index)].id);
                        }
                        break;

                    case ElementKind::Tab:
                        if (m_callbacks.on_tab_open
                            && hit.index >= 0
                            && hit.index < static_cast<i32>(kTabIdCount))
                        {
                            m_callbacks.on_tab_open(static_cast<TabId>(hit.index), TabOpenMode::ReplaceFocused);
                        }
                        break;

                    case ElementKind::TimePause:
                        if (m_callbacks.on_pause_toggle)
                        {
                            m_callbacks.on_pause_toggle();
                        }
                        break;

                    case ElementKind::TimeDown:
                        if (m_callbacks.on_time_scale_down)
                        {
                            m_callbacks.on_time_scale_down();
                        }
                        break;

                    case ElementKind::TimeUp:
                        if (m_callbacks.on_time_scale_up)
                        {
                            m_callbacks.on_time_scale_up();
                        }
                        break;

                    case ElementKind::TimeScale:
                        // TODO(Sprint 10+): open speed-set dropdown and call on_time_scale_set.
                        break;

                    case ElementKind::Stats:
                        // Read-only rows.
                        break;
                }
            }

            // TODO(Sprint 09 Task 9.11 / 9.12): wire right-click split menu
            // for tabs after InputEvent exposes full right-click context payload.
        }

        // Sidebar consumes all input while cursor is inside its region so center panes
        // do not receive mouse hover/click events behind it.
        return true;
    }

    void Sidebar::render(rendering::LineRenderer& lines,
                         PanelSystem& panel_system,
                         VkCommandBuffer cmd,
                         const VkExtent2D extent,
                         const ViewportRect& sidebar_rect) const
    {
        static_cast<void>(panel_system);
        static_cast<void>(cmd);

        const Vec2f viewport = {
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        };

        const std::vector<InteractiveElement> elements = build_interactive_elements(sidebar_rect);

        // 1) Background.
        draw_filled_rect(lines, sidebar_rect, kBackgroundColor, viewport);

        // 2) Right-edge border.
        const ViewportRect border_rect = {
            .x = sidebar_rect.right() - kBorderWidth,
            .y = sidebar_rect.y,
            .width = kBorderWidth,
            .height = sidebar_rect.height
        };
        draw_filled_rect(lines, border_rect, kBorderColor, viewport);

        const i32 content_x = static_cast<i32>(sidebar_rect.x + kPaddingX);
        i32 cursor_y = static_cast<i32>(sidebar_rect.y);

        i32 element_index = 0;

        auto draw_row_highlight = [&](const ViewportRect& rect, const Vec4f color)
        {
            draw_filled_rect(lines, rect, color, viewport);
        };

        auto is_hovered = [&](i32 index)
        {
            return m_hovered_row == index;
        };

        // -----------------------------------------------------------------
        // INSTRUMENTS
        // -----------------------------------------------------------------
        m_font.draw_text("INSTRUMENTS", static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kSectionHeaderColor);
        cursor_y += static_cast<i32>(kSectionHeaderH);

        for (i32 i = 0; i < static_cast<i32>(m_state.instruments.size()); ++i)
        {
            const InteractiveElement& row = elements[static_cast<std::size_t>(element_index)];
            const SidebarInstrumentEntry& entry = m_state.instruments[static_cast<std::size_t>(i)];

            const bool is_selected = !m_state.selected_instrument_id.empty()
                && entry.id == m_state.selected_instrument_id;
            if (is_selected)
            {
                draw_row_highlight(row.rect, kSelectedHighlight);
            }
            if (is_hovered(element_index))
            {
                draw_row_highlight(row.rect, kHoverHighlight);
            }

            m_font.draw_text(entry.name, static_cast<f32>(row.rect.x), static_cast<f32>(row.rect.y + 1), 1.0f, kContentColor);

            const f32 status_x = static_cast<f32>(row.rect.x) + (static_cast<f32>(entry.name.size()) + 2.0f) * kGlyphW;
            m_font.draw_text(entry.status, status_x, static_cast<f32>(row.rect.y + 1), 1.0f, kStatusColor);

            ++element_index;
        }

        cursor_y += static_cast<i32>(m_state.instruments.size()) * static_cast<i32>(kRowHeight);
        cursor_y += static_cast<i32>(kSectionGap);

        // -----------------------------------------------------------------
        // TABS
        // -----------------------------------------------------------------
        m_font.draw_text("TABS", static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kSectionHeaderColor);
        cursor_y += static_cast<i32>(kSectionHeaderH);

        for (i32 i = 0; i < static_cast<i32>(kTabIdCount); ++i)
        {
            const InteractiveElement& row = elements[static_cast<std::size_t>(element_index)];
            if (is_hovered(element_index))
            {
                draw_row_highlight(row.rect, kHoverHighlight);
            }

            const TabId tab = static_cast<TabId>(i);
            const bool is_visible = m_state.visible_tabs[static_cast<std::size_t>(i)];

            // TODO(Sprint 10+): switch to ▣/☐ markers once bitmap font supports these glyphs.
            const char marker = is_visible ? '#' : '-';
            const std::string label = std::format("{} {}", marker, to_display_name(tab));

            m_font.draw_text(label,
                             static_cast<f32>(row.rect.x),
                             static_cast<f32>(row.rect.y + 1),
                             1.0f,
                             is_visible ? kContentColor : kInactiveColor);

            ++element_index;
        }

        cursor_y += static_cast<i32>(kTabIdCount) * static_cast<i32>(kRowHeight);
        cursor_y += static_cast<i32>(kSectionGap);

        // -----------------------------------------------------------------
        // TIME
        // -----------------------------------------------------------------
        m_font.draw_text("TIME", static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kSectionHeaderColor);
        cursor_y += static_cast<i32>(kSectionHeaderH);

        const std::string pause_text = m_state.time.paused ? "[ > ]" : "[||]";
        const std::string scale_text = format_time_scale_label(m_state.time);

        const InteractiveElement& pause_elem = elements[static_cast<std::size_t>(element_index++)];
        const InteractiveElement& down_elem = elements[static_cast<std::size_t>(element_index++)];
        const InteractiveElement& scale_elem = elements[static_cast<std::size_t>(element_index++)];
        const InteractiveElement& up_elem = elements[static_cast<std::size_t>(element_index++)];

        if (is_hovered(element_index - 4))
        {
            draw_row_highlight(pause_elem.rect, kHoverHighlight);
        }
        if (is_hovered(element_index - 3))
        {
            draw_row_highlight(down_elem.rect, kHoverHighlight);
        }
        if (is_hovered(element_index - 2))
        {
            draw_row_highlight(scale_elem.rect, kHoverHighlight);
        }
        if (is_hovered(element_index - 1))
        {
            draw_row_highlight(up_elem.rect, kHoverHighlight);
        }

        m_font.draw_text(pause_text,
                         static_cast<f32>(pause_elem.rect.x + 2),
                         static_cast<f32>(pause_elem.rect.y + 1),
                         1.0f,
                         kContentColor);
        m_font.draw_text("-",
                         static_cast<f32>(down_elem.rect.x + 4),
                         static_cast<f32>(down_elem.rect.y + 1),
                         1.0f,
                         kContentColor);
        m_font.draw_text(scale_text,
                         static_cast<f32>(scale_elem.rect.x + 2),
                         static_cast<f32>(scale_elem.rect.y + 1),
                         1.0f,
                         kContentColor);
        m_font.draw_text("+",
                         static_cast<f32>(up_elem.rect.x + 4),
                         static_cast<f32>(up_elem.rect.y + 1),
                         1.0f,
                         kContentColor);

        cursor_y += static_cast<i32>(kRowHeight);
        cursor_y += static_cast<i32>(kSectionGap);

        // -----------------------------------------------------------------
        // STATS
        // -----------------------------------------------------------------
        m_font.draw_text("STATS", static_cast<f32>(content_x), static_cast<f32>(cursor_y), 1.0f, kSectionHeaderColor);
        cursor_y += static_cast<i32>(kSectionHeaderH);

        if (m_state.stats.empty())
        {
            // We intentionally keep the STATS header visible even when empty.
            m_font.draw_text("(no stats yet)", static_cast<f32>(content_x), static_cast<f32>(cursor_y + 1), 1.0f, kInactiveColor);
        }

        for (i32 i = 0; i < static_cast<i32>(m_state.stats.size()); ++i)
        {
            const InteractiveElement& row = elements[static_cast<std::size_t>(element_index)];
            const SidebarStatsEntry& stat = m_state.stats[static_cast<std::size_t>(i)];

            if (is_hovered(element_index))
            {
                draw_row_highlight(row.rect, kHoverHighlight);
            }

            m_font.draw_text(std::format("{}:", stat.label),
                             static_cast<f32>(row.rect.x),
                             static_cast<f32>(row.rect.y + 1),
                             1.0f,
                             kSectionHeaderColor);

            const f32 value_x = static_cast<f32>(row.rect.x) + (static_cast<f32>(stat.label.size()) + 2.0f) * kGlyphW;
            m_font.draw_text(stat.value,
                             value_x,
                             static_cast<f32>(row.rect.y + 1),
                             1.0f,
                             kContentColor);

            ++element_index;
        }
    }
}
