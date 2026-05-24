#pragma once

#include "core/types.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

namespace parallax::rendering
{
    class LineRenderer;
}

namespace parallax::ui
{
    class BitmapFont;
    class PanelSystem;
}

namespace parallax::ui::shell
{
    inline constexpr u32 kStatusBarHeight = 24;

    /// Notification severity controls colour.
    enum class NotificationSeverity : u8
    {
        Info,
        Warning,
        Error
    };

    /// A single notification queued by the Shell.
    struct NotificationItem
    {
        std::string message;
        std::chrono::steady_clock::time_point timestamp;
        NotificationSeverity severity = NotificationSeverity::Info;
    };

    /// Per-frame snapshot for the parts the Shell owns.
    struct StatusBarState
    {
        u32 active_session_count = 0;
        bool any_session_running = false;

        f64 time_scale = 1.0;
        bool time_paused = false;

        f32 fps = 0.0f;
    };

    /// @brief Persistent 24-px bar across the bottom of the window.
    ///
    /// Owns the notification rotation queue. Read-only otherwise.
    /// Consumes mouse input inside its rect.
    ///
    /// New notifications are queued FIFO but do not pre-empt the currently
    /// displayed item. Rotation advances after the current item's full display
    /// interval elapses.
    class StatusBar
    {
    public:
        explicit StatusBar(BitmapFont& font);

        StatusBar(const StatusBar&) = delete;
        StatusBar& operator=(const StatusBar&) = delete;
        StatusBar(StatusBar&&) = delete;
        StatusBar& operator=(StatusBar&&) = delete;

        void set_state(StatusBarState state);

        /// Push a notification. Shell forwards Shell::push_notification() here.
        /// Capped at @ref kMaxQueuedNotifications — oldest evicted FIFO.
        void push_notification(std::string message,
                               NotificationSeverity severity = NotificationSeverity::Info);

        /// Advance the rotating-notification timer. Call once per frame with dt seconds.
        void tick(f32 dt_seconds);

        /// @return Rect anchored at the BOTTOM of the window, full width,
        ///         height==kStatusBarHeight.
        [[nodiscard]] static ViewportRect compute_rect(const ViewportRect& window) noexcept;

        bool handle_input(const InputEvent& event, const ViewportRect& bar_rect);

        /// Render signature mirrors Sidebar (Task 9.3).
        void render(rendering::LineRenderer& lines,
                    PanelSystem& panel_system,
                    VkCommandBuffer cmd,
                    VkExtent2D extent,
                    const ViewportRect& bar_rect) const;

        /// Rotation interval — current notification displayed for this long
        /// before rotating to the next.
        static constexpr f32 kNotificationDisplaySeconds = 30.0f;

        /// Hard cap on queued notifications (rolling window).
        static constexpr std::size_t kMaxQueuedNotifications = 5;

    private:
        BitmapFont& m_font;
        StatusBarState m_state{};
        std::deque<NotificationItem> m_notifications;
        std::size_t m_current_index = 0;
        f32 m_time_in_current = 0.0f;
    };
}
