#pragma once

/// @file toolbar.hpp
/// @brief Stellarium-style bottom toolbar with auto-show/hide on mouse hover.
///
/// Three button groups arranged horizontally at the bottom screen edge:
///   1. Overlay toggles: CONST, STARS, DSO, GRID, HORIZ
///   2. Time controls:   <<, ||, >>, speed display
///   3. View:            FOV zoom slider
///
/// The toolbar appears when the mouse enters the bottom activation zone and
/// hides when the mouse leaves the toolbar area. Animated slide-up/down.
///
/// All buttons use text characters as icons (retro terminal aesthetic).
/// Each button is equivalent to its keyboard shortcut.
///
/// SPRINT 05 Task 5.3

#include "core/input.hpp"
#include "core/types.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace parallax::ui
{
    // =================================================================
    // ToolbarState — snapshot of application state for syncing toggles
    // =================================================================

    /// @brief Snapshot of application state used to sync toolbar toggle buttons.
    ///
    /// The Application fills this each frame so the toolbar can reflect
    /// the current state of overlays, time, and camera without coupling
    /// directly to Application internals.
    struct ToolbarState
    {
        // Overlay toggles
        bool constellations_visible = false;
        bool stars_visible          = true;
        bool dso_visible            = false;
        bool grid_visible           = false;
        bool horizon_visible        = false;

        // Time
        f64 time_scale = 1.0;
        bool time_paused = false;

        // Camera
        f64 fov_deg        = 60.0;
        f32 magnitude_limit = 6.5f;
    };

    // =================================================================
    // ToolbarCallbacks — actions the toolbar can invoke on Application
    // =================================================================

    /// @brief Callbacks from toolbar buttons back to the Application.
    ///
    /// Set these during initialization so buttons can trigger the same
    /// actions as their keyboard equivalents.
    struct ToolbarCallbacks
    {
        // Overlay toggles (equivalent to keyboard C, S, D, G, O)
        std::function<void()> toggle_constellations;
        std::function<void()> toggle_stars;
        std::function<void()> toggle_dso;
        std::function<void()> cycle_grid;
        std::function<void()> toggle_horizon;

        // Time controls (equivalent to keyboard -, Space, +, =)
        std::function<void()> time_reverse;
        std::function<void()> time_pause_toggle;
        std::function<void()> time_forward;
        std::function<void()> time_reset_now;

        // View controls
        std::function<void(f64)> set_fov;   ///< FOV slider changed
    };

    // =================================================================
    // Toolbar
    // =================================================================

    /// @brief Stellarium-style bottom toolbar with auto-show/hide.
    ///
    /// Behavior:
    ///   - Mouse enters bottom activation zone → toolbar slides up.
    ///   - Mouse leaves toolbar area → toolbar slides down.
    ///   - Toolbar is fully interactive while visible (buttons, sliders).
    ///   - Toggle buttons sync with application state each frame.
    ///
    /// Rendering: uses PanelSystem for the background, widgets for
    /// buttons/sliders, LineRenderer for separators, BitmapFont for text.
    class Toolbar
    {
    public:
        Toolbar() = default;
        ~Toolbar() = default;

        Toolbar(const Toolbar&) = delete;
        Toolbar& operator=(const Toolbar&) = delete;
        Toolbar(Toolbar&&) = delete;
        Toolbar& operator=(Toolbar&&) = delete;

        /// @brief Initialize toolbar widgets and layout.
        /// @param callbacks Actions for each button.
        void init(const ToolbarCallbacks& callbacks);

        /// @brief Sync toggle states with application and update hover/animation.
        /// @param mouse_pos      Current mouse position in screen pixels.
        /// @param mouse_clicked  True if the left mouse button was clicked this frame.
        /// @param mouse_down     True if the left mouse button is currently held.
        /// @param dt             Delta time in seconds.
        /// @param viewport_width  Current viewport width in pixels.
        /// @param viewport_height Current viewport height in pixels.
        /// @param state          Current application state for syncing toggles.
        void update(Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                    u32 viewport_width, u32 viewport_height,
                    const ToolbarState& state);

        /// @brief Render toolbar background, buttons, sliders, labels, separators.
        /// @param font          BitmapFont for text rendering.
        /// @param lines         LineRenderer for borders and separators.
        /// @param panel_system  PanelSystem for the toolbar background panel.
        /// @param cmd           Vulkan command buffer (for panel_system background).
        /// @param extent        Current viewport extent.
        void render(BitmapFont& font, rendering::LineRenderer& lines,
                    PanelSystem& panel_system,
                    VkCommandBuffer cmd, VkExtent2D extent) const;

        /// @brief True if the toolbar is currently visible (fully or partially).
        [[nodiscard]] bool is_visible() const;

        /// @brief True if the mouse is over the toolbar area.
        [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

        /// @brief True if any slider in the toolbar is being dragged.
        [[nodiscard]] bool is_dragging() const;

    private:
        void create_overlay_group(const ToolbarCallbacks& callbacks);
        void create_time_group(const ToolbarCallbacks& callbacks);
        void create_view_group(const ToolbarCallbacks& callbacks);

        void layout_widgets(u32 viewport_width, u32 viewport_height);

        // -----------------------------------------------------------------
        // Visibility / animation
        // -----------------------------------------------------------------

        /// @brief Height of the activation zone at the bottom of the screen (pixels).
        static constexpr f32 kActivationZone = 40.0f;

        /// @brief Total toolbar height (pixels).
        static constexpr f32 kToolbarHeight = 44.0f;

        /// @brief Slide animation speed (0→1 in seconds).
        static constexpr f32 kSlideSpeed = 6.0f;

        /// @brief Linger time after mouse leaves before hiding (seconds).
        static constexpr f32 kLingerTime = 0.5f;

        f32 m_slide_t = 0.0f;   ///< Animation progress: 0 = hidden, 1 = fully visible
        f32 m_linger_timer = 0.0f;
        bool m_mouse_in_zone = false;
        bool m_initialized = false;

        // -----------------------------------------------------------------
        // Layout
        // -----------------------------------------------------------------
        f32 m_toolbar_y   = 0.0f;   ///< Current top edge Y (animated)
        f32 m_toolbar_x   = 0.0f;   ///< Left edge X
        f32 m_toolbar_w   = 0.0f;   ///< Total width
        u32 m_viewport_w  = 0;
        u32 m_viewport_h  = 0;

        /// @brief Padding between button groups (pixels).
        static constexpr f32 kGroupSpacing = 16.0f;

        /// @brief Padding between buttons within a group (pixels).
        static constexpr f32 kButtonSpacing = 4.0f;

        /// @brief Standard button size (pixels).
        static constexpr f32 kButtonW = 48.0f;
        static constexpr f32 kButtonH = 28.0f;

        // -----------------------------------------------------------------
        // Widgets — Overlay group
        // -----------------------------------------------------------------
        std::unique_ptr<ToggleButton> m_btn_const;
        std::unique_ptr<ToggleButton> m_btn_stars;
        std::unique_ptr<ToggleButton> m_btn_dso;
        std::unique_ptr<ToggleButton> m_btn_grid;
        std::unique_ptr<ToggleButton> m_btn_horiz;

        // -----------------------------------------------------------------
        // Widgets — Time group
        // -----------------------------------------------------------------
        std::unique_ptr<Button> m_btn_time_rev;
        std::unique_ptr<Button> m_btn_time_pause;
        std::unique_ptr<Button> m_btn_time_fwd;
        std::unique_ptr<Button> m_btn_time_now;

        // -----------------------------------------------------------------
        // Widgets — View group
        // -----------------------------------------------------------------
        std::unique_ptr<Slider> m_slider_fov;

        // -----------------------------------------------------------------
        // Speed display (text only — rendered via BitmapFont)
        // -----------------------------------------------------------------
        std::string m_speed_text;
    };

} // namespace parallax::ui