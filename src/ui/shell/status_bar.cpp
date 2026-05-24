#include "ui/shell/status_bar.hpp"

#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"
#include "ui/widgets.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
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
        constexpr Vec3f kWarningColor{1.0f, 0.85f, 0.2f};
        constexpr Vec3f kErrorColor{1.0f, 0.3f, 0.3f};

        // TODO(Sprint 10+): restore Unicode glyphs once bitmap font supports them.
        constexpr const char* kSeparator = "|";
        constexpr const char* kSessionIcon = "*";
        constexpr const char* kInfoIcon = "+";
        constexpr const char* kWarningIcon = "!";
        constexpr const char* kErrorIcon = "x";
        constexpr const char* kPlayIcon = ">";
        constexpr const char* kPauseIcon = "||";

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

        [[nodiscard]] std::string format_time_scale(const StatusBarState& state)
        {
            if (state.time_paused)
            {
                return fmt::format("x{:.0f}", state.time_scale);
            }

            const f64 rounded = std::round(state.time_scale);
            if (std::abs(state.time_scale - rounded) < 0.0001)
            {
                return fmt::format("x{}", static_cast<i64>(rounded));
            }

            return fmt::format("x{:.1f}", state.time_scale);
        }

        [[nodiscard]] const char* severity_icon(const NotificationSeverity severity)
        {
            switch (severity)
            {
                case NotificationSeverity::Info:
                    return kInfoIcon;
                case NotificationSeverity::Warning:
                    return kWarningIcon;
                case NotificationSeverity::Error:
                    return kErrorIcon;
            }

            return kInfoIcon;
        }

        [[nodiscard]] Vec3f severity_color(const NotificationSeverity severity)
        {
            switch (severity)
            {
                case NotificationSeverity::Info:
                    return kContentColor;
                case NotificationSeverity::Warning:
                    return kWarningColor;
                case NotificationSeverity::Error:
                    return kErrorColor;
            }

            return kContentColor;
        }
    }

    StatusBar::StatusBar(BitmapFont& font)
        : m_font(font)
    {
    }

    void StatusBar::set_state(StatusBarState state)
    {
        m_state = std::move(state);
    }

    void StatusBar::push_notification(std::string message, NotificationSeverity severity)
    {
        const bool was_empty = m_notifications.empty();

        m_notifications.push_back(NotificationItem{
            .message = std::move(message),
            .timestamp = std::chrono::steady_clock::now(),
            .severity = severity
        });

        if (m_notifications.size() > kMaxQueuedNotifications)
        {
            m_notifications.pop_front();
            if (m_current_index > 0)
            {
                --m_current_index;
            }
            else
            {
                m_current_index = 0;
            }
        }

        if (was_empty)
        {
            m_current_index = 0;
            m_time_in_current = 0.0f;
        }
    }

    void StatusBar::tick(const f32 dt_seconds)
    {
        if (m_notifications.empty())
        {
            m_current_index = 0;
            m_time_in_current = 0.0f;
            return;
        }

        m_time_in_current += std::max(dt_seconds, 0.0f);

        if (m_time_in_current >= kNotificationDisplaySeconds)
        {
            m_current_index = (m_current_index + 1) % m_notifications.size();
            m_time_in_current = 0.0f;
        }
    }

    ViewportRect StatusBar::compute_rect(const ViewportRect& window) noexcept
    {
        return ViewportRect{
            .x = window.x,
            .y = window.bottom() - kStatusBarHeight,
            .width = window.width,
            .height = kStatusBarHeight
        };
    }

    bool StatusBar::handle_input(const InputEvent& event, const ViewportRect& bar_rect)
    {
        return event.inside_viewport && bar_rect.contains(event.mouse_pos);
    }

    void StatusBar::render(rendering::LineRenderer& lines,
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
            .y = bar_rect.y,
            .width = bar_rect.width,
            .height = kBorderWidth
        };
        draw_filled_rect(lines, border_rect, kBorderColor, viewport);

        const std::string fps_text = fmt::format("FPS {:.0f}", m_state.fps);
        const i32 fps_x = static_cast<i32>(bar_rect.right()) - text_width_px(fps_text) - static_cast<i32>(kPaddingX);
        const i32 text_y = static_cast<i32>(bar_rect.y + (bar_rect.height - static_cast<u32>(kGlyphH)) / 2u);

        i32 cursor_x = static_cast<i32>(bar_rect.x + kPaddingX);

        auto draw_section = [&](const std::string& text, const Vec3f color)
        {
            if (text.empty())
            {
                return;
            }

            const i32 x_px = std::max(cursor_x, static_cast<i32>(bar_rect.x));
            m_font.draw_text(text, static_cast<f32>(x_px), static_cast<f32>(text_y), 1.0f, color);
            cursor_x += text_width_px(text);
            cursor_x += static_cast<i32>(kGlyphW);
        };

        auto draw_separator = [&]()
        {
            draw_section(kSeparator, kSectionHeaderColor);
        };

        std::string sessions_text = fmt::format("{} {} active sessions", kSessionIcon, m_state.active_session_count);
        if (m_state.any_session_running)
        {
            sessions_text += " [running]";
        }
        draw_section(sessions_text, kContentColor);

        draw_separator();

        if (!m_notifications.empty() && m_current_index < m_notifications.size())
        {
            const NotificationItem& current = m_notifications[m_current_index];
            const std::string notification_text = fmt::format("{} {}", severity_icon(current.severity), current.message);
            draw_section(notification_text, severity_color(current.severity));
        }

        draw_separator();

        const std::string time_text = fmt::format("{} {}",
                                                  format_time_scale(m_state),
                                                  m_state.time_paused ? kPauseIcon : kPlayIcon);
        draw_section(time_text, kContentColor);

        m_font.draw_text(fps_text, static_cast<f32>(fps_x), static_cast<f32>(text_y), 1.0f, kInactiveColor);
    }
}
