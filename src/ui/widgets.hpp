#pragma once

/// @file widgets.hpp
/// @brief Clickable UI widgets for the retro terminal interface.
///
/// Three widget types built on the Panel system (Task 5.1):
///   - Button:       green border, centered text, hover highlight, click flash.
///   - ToggleButton: filled when active, outlined when inactive.
///   - Slider:       horizontal track with draggable handle, value display.
///
/// All widgets render using BitmapFont (text) and LineRenderer (borders/tracks).
/// Positions are in screen-space pixels (origin top-left).
///
/// SPRINT 05 Task 5.2

#include "core/types.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/panel_system.hpp"

#include <functional>
#include <string>

namespace parallax::ui
{
    // =================================================================
    // Constants
    // =================================================================

    /// @brief Default retro green colors for widgets.
    namespace widget_colors
    {
        inline constexpr Vec4f kBorder       = {0.0f, 0.6f, 0.0f, 0.8f};
        inline constexpr Vec4f kBorderBright = {0.0f, 1.0f, 0.0f, 1.0f};
        inline constexpr Vec4f kHighlight    = {0.0f, 1.0f, 0.0f, 0.3f};
        inline constexpr Vec4f kActiveFill   = {0.0f, 0.3f, 0.0f, 0.7f};
        inline constexpr Vec4f kFlash        = {0.0f, 1.0f, 0.0f, 0.6f};
        inline constexpr Vec4f kTrack        = {0.0f, 0.3f, 0.0f, 0.5f};
        inline constexpr Vec4f kHandle       = {0.0f, 1.0f, 0.0f, 1.0f};

        inline constexpr Vec3f kTextBright   = {0.0f, 1.0f, 0.0f};
        inline constexpr Vec3f kTextDim      = {0.0f, 0.6f, 0.0f};
    } // namespace widget_colors

    /// @brief Click flash duration in seconds.
    inline constexpr f32 kFlashDuration = 0.12f;

    // =================================================================
    // Button
    // =================================================================

    /// @brief A clickable button with green border, centered text, hover highlight.
    ///
    /// Visual states:
    ///   - Normal:  green border, dim text.
    ///   - Hovered: bright border, highlight fill, bright text.
    ///   - Flash:   brief bright fill on click.
    class Button
    {
    public:
        /// @brief Construct a button.
        /// @param label    Display text (centered in the button rect).
        /// @param position Top-left corner in screen pixels.
        /// @param size     Width × Height in pixels.
        /// @param on_click Callback invoked when clicked.
        Button(const std::string& label, Vec2f position, Vec2f size,
               std::function<void()> on_click);

        virtual ~Button() = default;

        Button(const Button&) = delete;
        Button& operator=(const Button&) = delete;
        Button(Button&&) = default;
        Button& operator=(Button&&) = default;

        /// @brief Update hover/click state from mouse input.
        /// @param mouse_pos     Current mouse position in screen pixels.
        /// @param mouse_clicked True if the left mouse button was clicked this frame.
        /// @param dt            Delta time in seconds (for flash animation).
        void update(Vec2f mouse_pos, bool mouse_clicked, f32 dt);

        /// @brief Render the button using LineRenderer (borders) and BitmapFont (text).
        /// @param font          The bitmap font for label rendering.
        /// @param lines         The line renderer for border drawing.
        /// @param viewport_size Current viewport width × height in pixels.
        void render(BitmapFont& font, rendering::LineRenderer& lines,
                    Vec2f viewport_size) const;

        /// @brief True if the mouse is currently over this button.
        [[nodiscard]] bool is_hovered() const;

        /// @brief True if the button is currently in the click-flash state.
        [[nodiscard]] bool is_flashing() const;

        /// @brief Check if a screen-space point is inside this button rect.
        [[nodiscard]] bool contains(Vec2f point) const;

        // Accessors
        void set_position(Vec2f pos);
        void set_label(const std::string& label);
        [[nodiscard]] Vec2f get_position() const;
        [[nodiscard]] Vec2f get_size() const;
        [[nodiscard]] const std::string& get_label() const;

    protected:
        std::string m_label;
        Vec2f m_position;
        Vec2f m_size;
        std::function<void()> m_on_click;

        bool m_hovered  = false;
        bool m_pressed  = false;
        f32 m_flash_timer = 0.0f;   ///< Remaining flash time in seconds

        /// @brief Convert pixel rect to NDC rect for LineRenderer.
        [[nodiscard]] static Vec2f pixel_to_ndc(Vec2f px, Vec2f viewport);
    };

    // =================================================================
    // ToggleButton
    // =================================================================

    /// @brief A toggle button that shows ON/OFF state visually.
    ///
    /// Active:   filled dark green background + bright border + bright text.
    /// Inactive: no fill, dim border, dim text.
    class ToggleButton : public Button
    {
    public:
        /// @brief Construct a toggle button.
        /// @param label    Display text.
        /// @param position Top-left corner in screen pixels.
        /// @param size     Width × Height in pixels.
        /// @param on_click Callback invoked when toggled (after state flip).
        ToggleButton(const std::string& label, Vec2f position, Vec2f size,
                     std::function<void()> on_click);

        ~ToggleButton() override = default;

        /// @brief Update with automatic toggle on click.
        void update(Vec2f mouse_pos, bool mouse_clicked, f32 dt);

        /// @brief Render with active/inactive state distinction.
        void render(BitmapFont& font, rendering::LineRenderer& lines,
                    Vec2f viewport_size) const;

        /// @brief Get the current active state.
        [[nodiscard]] bool is_active() const;

        /// @brief Set the active state programmatically (e.g., from keyboard shortcut).
        void set_active(bool active);

        /// @brief Toggle the active state.
        void toggle();

    private:
        bool m_active = false;
    };

    // =================================================================
    // Slider
    // =================================================================

    /// @brief A horizontal slider with a draggable handle and value display.
    ///
    /// Layout (left to right):
    ///   [label] ═══●═══ [value]
    ///
    /// The track and handle are drawn with LineRenderer.
    /// The label and value text use BitmapFont.
    class Slider
    {
    public:
        /// @brief Construct a slider.
        /// @param label    Label text rendered to the left of the track.
        /// @param position Top-left corner in screen pixels.
        /// @param width    Total widget width in pixels (including label + value).
        /// @param min_val  Minimum slider value.
        /// @param max_val  Maximum slider value.
        /// @param step     Snap step (0 = continuous).
        Slider(const std::string& label, Vec2f position, f32 width,
               f32 min_val, f32 max_val, f32 step = 0.0f);

        ~Slider() = default;

        Slider(const Slider&) = delete;
        Slider& operator=(const Slider&) = delete;
        Slider(Slider&&) = default;
        Slider& operator=(Slider&&) = default;

        /// @brief Update drag state from mouse input.
        /// @param mouse_pos  Current mouse position in screen pixels.
        /// @param mouse_down True if the left mouse button is currently held.
        /// @param mouse_clicked True if the left mouse button was clicked this frame.
        /// @param dt         Delta time in seconds.
        void update(Vec2f mouse_pos, bool mouse_down, bool mouse_clicked, f32 dt);

        /// @brief Render the slider track, handle, label, and value.
        void render(BitmapFont& font, rendering::LineRenderer& lines,
                    Vec2f viewport_size) const;

        /// @brief Get the current value.
        [[nodiscard]] f32 get_value() const;

        /// @brief Set the value programmatically (clamped and snapped).
        void set_value(f32 val);

        /// @brief Check if the mouse is over the slider track area.
        [[nodiscard]] bool contains(Vec2f point) const;

        /// @brief True if the handle is being dragged.
        [[nodiscard]] bool is_dragging() const;

        // Accessors
        void set_position(Vec2f pos);
        void set_format_string(const std::string& fmt);
        [[nodiscard]] Vec2f get_position() const;
        [[nodiscard]] f32 get_width() const;

        /// @brief Optional callback invoked when the value changes.
        std::function<void(f32)> on_value_changed;

    private:
        /// @brief Compute the track rect (the draggable area, excluding label/value text).
        void compute_track_rect();

        /// @brief Snap a value to the step grid.
        [[nodiscard]] f32 snap_value(f32 val) const;

        /// @brief Convert pixel to NDC.
        [[nodiscard]] static Vec2f pixel_to_ndc(Vec2f px, Vec2f viewport);

        std::string m_label;
        std::string m_format = "{:.1f}";   ///< fmt format string for value display
        Vec2f m_position;
        f32 m_width;
        f32 m_value;
        f32 m_min;
        f32 m_max;
        f32 m_step;

        // Track geometry (in pixels, computed from position + width)
        f32 m_track_x      = 0.0f;   ///< Track left edge
        f32 m_track_width   = 0.0f;   ///< Track drawable width
        f32 m_track_y       = 0.0f;   ///< Track center Y

        bool m_dragging = false;
        bool m_hovered  = false;

        /// @brief Height of the slider widget in pixels.
        static constexpr f32 kSliderHeight = 20.0f;
        /// @brief Handle radius in pixels.
        static constexpr f32 kHandleRadius = 5.0f;
        /// @brief Track thickness in pixels.
        static constexpr f32 kTrackThickness = 2.0f;
        /// @brief Width reserved for label text (pixels).
        static constexpr f32 kLabelWidth = 0.0f;  ///< Computed from label length
        /// @brief Width reserved for value text (pixels).
        static constexpr f32 kValueWidth = 56.0f;
    };

} // namespace parallax::ui