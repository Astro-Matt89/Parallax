#pragma once

#include "core/types.hpp"
#include "ui/shell/shell_types.hpp"
#include "ui/shell/tab_id.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

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
    /// Fixed pixel width of the sidebar (left edge of the shell).
    inline constexpr u32 kSidebarWidth = 200;

    /// Snapshot of one row in the INSTRUMENTS section.
    struct SidebarInstrumentEntry
    {
        std::string id;      ///< Stable instrument id (e.g. "mock_instrument").
        std::string name;    ///< Display name (e.g. "Mock Instrument").
        std::string status;  ///< Short status word (e.g. "idle", "running").
    };

    /// Snapshot of the time-control state shown in the TIME section.
    struct SidebarTimeState
    {
        bool paused = false;
        f64 time_scale = 1.0;  ///< Multiplier; 1.0 = realtime.
    };

    /// Optional STATS section content. Each entry is a (label, value) pair.
    struct SidebarStatsEntry
    {
        std::string label;
        std::string value;
    };

    /// Per-frame snapshot pushed into the Sidebar by the Shell.
    struct SidebarState
    {
        std::vector<SidebarInstrumentEntry> instruments;
        std::string selected_instrument_id;  ///< empty = none.

        /// Which tabs are currently visible in any leaf of the PaneTree.
        /// Bit i set ⇒ TabId(i) is visible. Size: kTabIdCount.
        std::array<bool, kTabIdCount> visible_tabs{};

        SidebarTimeState time;
        std::vector<SidebarStatsEntry> stats;
    };

    /// Right-click "open in split" direction passed to the tab-open callback.
    enum class TabOpenMode : u8
    {
        ReplaceFocused,  ///< Plain left-click: open in currently focused pane.
        SplitRight,      ///< Right-click → "Open in new split (right)".
        SplitBottom      ///< Right-click → "Open in new split (bottom)".
    };

    /// Callback signatures pushed in by the Shell. All are owned by the caller.
    ///
    /// Keyboard shortcuts are not handled directly here. In Task 9.11, the Shell
    /// should bind existing keyboard semantics to these callbacks.
    struct SidebarCallbacks
    {
        std::function<void(TabId, TabOpenMode)> on_tab_open;
        std::function<void(std::string_view)> on_instrument_selected;  // by id
        std::function<void()> on_pause_toggle;
        std::function<void(f64)> on_time_scale_set;  // new scale
        std::function<void()> on_time_scale_up;
        std::function<void()> on_time_scale_down;
    };

    /// @brief Fixed-width left sidebar containing INSTRUMENTS / TABS / TIME / STATS.
    ///
    /// The Sidebar always occupies the leftmost @ref kSidebarWidth pixels of
    /// the area between the top bar and the status bar. It owns no game state
    /// — every frame the Shell calls @ref set_state with a fresh snapshot, and
    /// the Sidebar reflects that snapshot when rendering and when generating
    /// callbacks during @ref handle_input.
    ///
    /// Input ordering: the Shell must dispatch input to the Sidebar BEFORE
    /// routing to the central pane area. If @ref handle_input returns true
    /// the event was consumed and must not be forwarded.
    ///
    /// Render signature: mirrors the existing floating UI render path
    /// (`BitmapFont` + `LineRenderer` + `VkExtent2D`) so this scaffolding can
    /// reuse the same pixel-space drawing pipeline used by Toolbar/SidePanel.
    class Sidebar
    {
    public:
        explicit Sidebar(BitmapFont& font);

        Sidebar(const Sidebar&) = delete;
        Sidebar& operator=(const Sidebar&) = delete;
        Sidebar(Sidebar&&) = delete;
        Sidebar& operator=(Sidebar&&) = delete;

        void set_callbacks(SidebarCallbacks callbacks);
        void set_state(SidebarState state);

        /// @return The viewport region this sidebar will occupy given the
        ///         caller's available rect (between top and status bars).
        ///         The returned rect always has width == kSidebarWidth.
        [[nodiscard]] static ViewportRect compute_rect(const ViewportRect& available) noexcept;

        /// Handle mouse input. @p event has already been translated into
        /// framebuffer-pixel coordinates (NOT viewport-local). Returns true
        /// if the event was inside the sidebar and consumed.
        bool handle_input(const InputEvent& event, const ViewportRect& sidebar_rect);

        /// Render the sidebar background, border and all sections.
        void render(rendering::LineRenderer& lines,
                    PanelSystem& panel_system,
                    VkCommandBuffer cmd,
                    VkExtent2D extent,
                    const ViewportRect& sidebar_rect) const;

    private:
        enum class ElementKind : u8
        {
            Instrument,
            Tab,
            TimePause,
            TimeDown,
            TimeScale,
            TimeUp,
            Stats
        };

        struct InteractiveElement
        {
            ElementKind kind = ElementKind::Stats;
            ViewportRect rect{};
            i32 index = -1;  ///< instrument/stats index, or tab index.
        };

        [[nodiscard]] std::vector<InteractiveElement> build_interactive_elements(
            const ViewportRect& sidebar_rect) const;

        [[nodiscard]] static bool contains(const ViewportRect& rect, const Vec2f& point) noexcept;

        BitmapFont& m_font;
        SidebarCallbacks m_callbacks{};
        SidebarState m_state{};
        i32 m_hovered_row = -1;
    };
}
