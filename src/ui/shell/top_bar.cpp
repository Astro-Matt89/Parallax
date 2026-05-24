#include "ui/shell/top_bar.hpp"

#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"
#include "ui/widgets.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <utility>

namespace parallax::ui::shell
{
    namespace
    {
        constexpr u32 kPaddingX = 8;
        constexpr u32 kBorderWidth = 1;
        constexpr f32 kGlyphW = 8.0f;
        constexpr f32 kGlyphH = 16.0f;

        // TODO: deduplicate shell colour palette shared by sidebar/top_bar/status_bar.
        constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec4f kBorderColor = widget_colors::kBorderBright;
        constexpr Vec3f kSectionHeaderColor = widget_colors::kTextDim;
        constexpr Vec3f kContentColor = widget_colors::kTextBright;
        constexpr Vec3f kInactiveColor{0.0f, 0.45f, 0.0f};

        // TODO(Sprint 10+): restore Unicode separator once bitmap font supports it.
        constexpr const char* kSeparator = "|";

        [[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport)
        {
            return {
                (px.x / viewport.x) * 2.0f - 1.0f,
                (px.y / viewport.y) * 2.0f - 1.0f
            };
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
                lines.add_line(pixel_to_ndc({x0, py}, viewport), pixel_to_ndc({x1, py}, viewport), color);
            }
        }

        [[nodiscard]] i32 text_width_px(const std::string& text)
        {
            return static_cast<i32>(text.size() * 8);
        }
    }

    TopBar::TopBar(BitmapFont& font)
        : m_font(font)
    {
    }

    void TopBar::set_state(TopBarState state)
    {
        m_state = std::move(state);
    }

    ViewportRect TopBar::compute_rect(const ViewportRect& window) noexcept
    {
        return ViewportRect{
            .x = window.x,
            .y = 0,
            .width = window.width,
            .height = kTopBarHeight
        };
    }

    bool TopBar::handle_input(const InputEvent& event, const ViewportRect& bar_rect)
    {
        return event.inside_viewport && bar_rect.contains(event.mouse_pos);
    }

    void TopBar::render(rendering::LineRenderer& lines,
                        PanelSystem& panel_system,
                        VkCommandBuffer cmd,
                        const VkExtent2D extent,
                        const ViewportRect& bar_rect) const
    {
        static_cast<void>(panel_system);
        static_cast<void>(cmd);

        const Vec2f viewport = {
            static_cast<f32>(extent.width),
            static_cast<f32>(extent.height)
        };

        draw_filled_rect(lines, bar_rect, kBackgroundColor, viewport);

        const ViewportRect border_rect = {
            .x = bar_rect.x,
            .y = bar_rect.bottom() - kBorderWidth,
            .width = bar_rect.width,
            .height = kBorderWidth
        };
        draw_filled_rect(lines, border_rect, kBorderColor, viewport);

        i32 cursor_x = static_cast<i32>(bar_rect.x + kPaddingX);
        const i32 text_y = static_cast<i32>(bar_rect.y + (bar_rect.height - static_cast<u32>(kGlyphH)) / 2u);

        auto draw_section = [&](const std::string& text, const Vec3f color)
        {
            const i32 x_px = std::max(cursor_x, static_cast<i32>(bar_rect.x));
            m_font.draw_text(text, static_cast<f32>(x_px), static_cast<f32>(text_y), 1.0f, color);
            cursor_x += text_width_px(text);
            cursor_x += static_cast<i32>(kGlyphW);
        };

        auto draw_separator = [&]()
        {
            draw_section(kSeparator, kSectionHeaderColor);
        };

        draw_section("PARALLAX", kContentColor);
        draw_separator();

        draw_section(m_state.location_name, kContentColor);
        draw_separator();

        const std::string jd_and_time = fmt::format("{}  {}",
                                                    fmt::format("JD {:.2f}", m_state.julian_date),
                                                    m_state.civil_time);
        draw_section(jd_and_time, kContentColor);
        draw_separator();

        if (m_state.vacuum_site)
        {
            draw_section("Atmo --", kInactiveColor);
        }
        else
        {
            draw_section(m_state.atmosphere_on ? "Atmo ON" : "Atmo OFF",
                         m_state.atmosphere_on ? kContentColor : kInactiveColor);
        }
        draw_separator();

        if (m_state.vacuum_site)
        {
            draw_section("VACUUM", kContentColor);
        }
        else
        {
            draw_section(fmt::format("Bortle {:.1f}", m_state.bortle_scale), kContentColor);
        }
    }
}
