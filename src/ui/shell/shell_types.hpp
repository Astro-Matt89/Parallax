/// @file shell_types.hpp
/// @brief Common enums and value types shared across the UI shell.
///
/// SPRINT 09 Task 9.1 — Shell: Core Types and Layout.
///
/// This header collects the small, header-only types that the rest of the
/// shell (panes, tabs, sidebar, top/status bars) depends on:
///   - PaneKind        — leaf vs split-direction discriminator.
///   - SplitterAxis    — orientation of a splitter for hit-testing.
///   - MouseButton     — abstract mouse button identifier.
///   - InputEvent      — viewport-local input snapshot passed to TabContent.
///
/// Note on InputEvent: the project currently feeds raw SDL events into a
/// polled core::Input state tracker. The shell needs a slightly higher-level
/// view: a per-frame "what is the cursor doing inside this viewport?"
/// snapshot. Subsequent tasks (9.2 pane hit-testing, 9.5 planetarium tab,
/// 9.11 shell integration) will fill out the adapter that translates
/// core::Input into InputEvent for each visible pane.

#pragma once

#include "core/types.hpp"

#include <cstdint>

namespace parallax::ui::shell
{
    // ------------------------------------------------------------------
    // PaneKind
    // ------------------------------------------------------------------

    /// @brief Discriminator for the recursive Pane tree (see Task 9.2).
    ///
    /// A Pane is either a `Leaf` (which holds one or more tabs, of which one
    /// is currently active and rendered) or an internal node that splits its
    /// area between two child panes either `HorizontalSplit` (children side
    /// by side) or `VerticalSplit` (children stacked top/bottom).
    enum class PaneKind : u8
    {
        Leaf,             ///< Terminal pane that hosts tabs.
        HorizontalSplit,  ///< Two children laid out left/right.
        VerticalSplit     ///< Two children laid out top/bottom.
    };

    /// @return Human-readable name of @p kind for logging/debugging.
    [[nodiscard]] constexpr const char* to_string(PaneKind kind) noexcept
    {
        switch (kind)
        {
            case PaneKind::Leaf:            return "Leaf";
            case PaneKind::HorizontalSplit: return "HorizontalSplit";
            case PaneKind::VerticalSplit:   return "VerticalSplit";
        }
        return "Unknown";
    }

    // ------------------------------------------------------------------
    // SplitterAxis
    // ------------------------------------------------------------------

    /// @brief Visual orientation of a splitter line between two child panes.
    ///
    /// A `HorizontalSplit` pane (children left/right) produces a vertical
    /// splitter line, and vice versa. Encoding the *line* orientation rather
    /// than the *split direction* simplifies hit-testing and drag math.
    enum class SplitterAxis : u8
    {
        Vertical,    ///< Splitter line is vertical; drag moves it horizontally.
        Horizontal   ///< Splitter line is horizontal; drag moves it vertically.
    };

    // ------------------------------------------------------------------
    // MouseButton
    // ------------------------------------------------------------------

    /// @brief Abstract mouse button identifier used by InputEvent.
    enum class MouseButton : u8
    {
        None,
        Left,
        Right,
        Middle
    };

    // ------------------------------------------------------------------
    // InputEvent
    // ------------------------------------------------------------------

    /// @brief Per-frame, viewport-local input snapshot delivered to a tab.
    ///
    /// The shell routes one InputEvent per frame to the active leaf pane.
    /// Tab implementations should treat this as the only source of input
    /// truth while their tab is focused — they must NOT poll core::Input
    /// directly, because input may be claimed by sidebar/splitter dragging,
    /// or routed to a different pane.
    ///
    /// Coordinate convention for @ref mouse_pos and @ref click_pos:
    ///   - When the event is delivered to a TabContent, the shell translates
    ///     these positions into coordinates relative to the tab's viewport
    ///     (i.e. (0, 0) is the top-left of the viewport, NOT of the window).
    ///   - If the cursor falls outside the viewport, @ref inside_viewport is
    ///     false and tabs should ignore mouse input.
    ///
    /// Keyboard state is intentionally NOT mirrored here for Sprint 09 —
    /// tabs that need keyboard input (e.g. PlanetariumTab keyboard shortcuts)
    /// will consult core::Input directly, gated on whether their pane is the
    /// currently focused one. This avoids duplicating the SDL_Scancode set
    /// while keeping the shell event surface small.
    struct InputEvent
    {
        /// Cursor position in viewport-local pixels (top-left origin).
        /// Only meaningful when @ref inside_viewport is true.
        Vec2f mouse_pos{0.0f, 0.0f};

        /// Cursor delta since the previous frame, in pixels.
        Vec2f mouse_delta{0.0f, 0.0f};

        /// True if the cursor is currently inside the receiving viewport.
        /// When false, tabs must ignore mouse_pos / click_pos / drag_delta.
        bool inside_viewport = false;

        /// True for exactly one frame when a click (press+release without
        /// crossing the drag threshold) was registered inside the viewport.
        bool was_click = false;

        /// Position of the click that completed this frame, viewport-local.
        /// Only meaningful when @ref was_click is true.
        Vec2f click_pos{0.0f, 0.0f};

        /// Which button produced the click. Sprint 09 only emits Left.
        MouseButton click_button = MouseButton::None;

        /// True while the user is mid-drag with the left button held and
        /// the drag threshold already crossed.
        bool is_dragging = false;

        /// Drag delta accumulated this frame, in viewport-local pixels.
        /// Zero when not dragging.
        Vec2f drag_delta{0.0f, 0.0f};

        /// Scroll wheel delta this frame (positive = scroll up / zoom in).
        f32 scroll_delta = 0.0f;
    };
}
