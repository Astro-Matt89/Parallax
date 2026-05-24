#pragma once

#include "core/types.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

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
    /// Fixed pixel height of the top bar (spans full window width).
    inline constexpr u32 kTopBarHeight = 32;

    /// Per-frame snapshot pushed in by the Shell.
    struct TopBarState
    {
        std::string location_name;
        f64 julian_date = 0.0;
        std::string civil_time;
        bool atmosphere_on = true;
        bool vacuum_site = false;
        f32 bortle_scale = 4.0f;
    };

    /// @brief Persistent 32-px bar across the top of the window.
    ///
    /// Read-only display. Updates every frame for time. Consumes any mouse
    /// input inside its rect (no pan/zoom through).
    class TopBar
    {
    public:
        explicit TopBar(BitmapFont& font);

        TopBar(const TopBar&) = delete;
        TopBar& operator=(const TopBar&) = delete;
        TopBar(TopBar&&) = delete;
        TopBar& operator=(TopBar&&) = delete;

        void set_state(TopBarState state);

        /// @return Rect for the top bar given the full window framebuffer rect.
        ///         Always anchored at y=0, height==kTopBarHeight, full width.
        [[nodiscard]] static ViewportRect compute_rect(const ViewportRect& window) noexcept;

        /// Returns true if the event was inside this bar (and therefore consumed).
        bool handle_input(const InputEvent& event, const ViewportRect& bar_rect);

        /// Render signature mirrors Sidebar (Task 9.3).
        void render(rendering::LineRenderer& lines,
                    PanelSystem& panel_system,
                    VkCommandBuffer cmd,
                    VkExtent2D extent,
                    const ViewportRect& bar_rect) const;

    private:
        BitmapFont& m_font;
        TopBarState m_state{};
    };
}
