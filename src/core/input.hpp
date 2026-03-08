#pragma once

/// @file input.hpp
/// @brief SDL2 input state tracker: click vs drag, scroll, keyboard, cursor management.
///
/// Input only tracks state — it does NOT modify the camera or any other system.
/// The Application loop reads Input state and translates it to Camera/simulation actions.
///
/// SPRINT 05 Task 5.6: Click vs drag distinction, mouse position tracking, cursor styles.

#include "core/types.hpp"

#include <SDL2/SDL.h>

#include <unordered_set>

namespace parallax::core
{

    /// @brief Visual cursor style for different interaction zones.
    ///
    /// The Application sets the active cursor based on what the mouse
    /// is hovering over; Input manages the SDL_Cursor lifetimes.
    enum class CursorStyle : u8
    {
        Arrow,       ///< Default — general navigation
        Hand,        ///< Over a clickable UI element
        Crosshair,   ///< Over the sky (ready to select)
        SizeAll      ///< Dragging the sky (panning)
    };

    /// @brief Tracks per-frame input state from SDL2 events.
    ///
    /// Usage pattern each frame:
    ///   1. Call new_frame() to reset per-frame deltas
    ///   2. For each SDL_Event from Window::poll_events(), call process_event()
    ///   3. Query state: was_click(), is_mouse_dragging(), get_mouse_drag_delta(), etc.
    ///   4. Application translates state to Camera::pan(), Selection::try_select(), etc.
    ///
    /// Click vs drag distinction:
    ///   - On MOUSEBUTTONDOWN: record press position.
    ///   - On MOUSEMOTION while button held: accumulate movement.
    ///     If total distance from press > kDragThreshold → drag mode.
    ///   - On MOUSEBUTTONUP: if still within threshold → click. Otherwise → end drag.
    ///   - Click is reported for exactly one frame (the frame of release).
    class Input
    {
    public:
        Input();
        ~Input();

        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;
        Input(Input&&) = delete;
        Input& operator=(Input&&) = delete;

        /// @brief Process a single SDL event. Call for each event polled this frame.
        /// @param event The SDL event to process.
        void process_event(const SDL_Event& event);

        /// @brief Reset per-frame state. Call at the start of each frame before processing events.
        void new_frame();

        // -----------------------------------------------------------------
        // Mouse state — click vs drag
        // -----------------------------------------------------------------

        /// @brief True if a left-click occurred this frame (press + release within threshold).
        ///
        /// A click is NOT a drag. This is only true on the frame the button was released
        /// within kDragThreshold pixels of the press position.
        [[nodiscard]] bool was_click() const;

        /// @brief Get the screen position where the click occurred (valid when was_click() is true).
        /// Returns the release position in screen pixels.
        [[nodiscard]] Vec2f get_click_position() const;

        /// @brief True if the left mouse button is held and has moved past the drag threshold.
        [[nodiscard]] bool is_mouse_dragging() const;

        /// @brief Accumulated mouse drag delta this frame in pixels (x, y).
        /// Positive x = rightward, positive y = downward (SDL screen coords).
        /// Only non-zero when is_mouse_dragging() is true.
        [[nodiscard]] Vec2f get_mouse_drag_delta() const;

        /// @brief True if the left mouse button is currently held down (drag OR pre-drag).
        [[nodiscard]] bool is_left_button_down() const;

        // -----------------------------------------------------------------
        // Mouse position
        // -----------------------------------------------------------------

        /// @brief Current mouse position in screen pixels (updated every frame).
        [[nodiscard]] Vec2f get_mouse_position() const;

        // -----------------------------------------------------------------
        // Scroll
        // -----------------------------------------------------------------

        /// @brief Accumulated scroll wheel delta this frame.
        /// Positive = scroll up (zoom in), negative = scroll down (zoom out).
        [[nodiscard]] f32 get_scroll_delta() const;

        // -----------------------------------------------------------------
        // Keyboard state
        // -----------------------------------------------------------------

        /// @brief True if the key was pressed (went down) THIS frame only.
        /// Use for toggle actions (e.g., Space to pause/resume, R to reset).
        [[nodiscard]] bool is_key_pressed(SDL_Scancode key) const;

        /// @brief True if the key is currently held down.
        /// Use for continuous actions (e.g., arrow keys for panning).
        [[nodiscard]] bool is_key_held(SDL_Scancode key) const;

        // -----------------------------------------------------------------
        // Cursor management
        // -----------------------------------------------------------------

        /// @brief Set the visual cursor style.
        /// Only changes the cursor if the style has actually changed.
        /// @param style The desired cursor appearance.
        void set_cursor(CursorStyle style);

        /// @brief Get the current cursor style.
        [[nodiscard]] CursorStyle get_cursor_style() const;

    private:
        // -----------------------------------------------------------------
        // Click vs drag state machine
        // -----------------------------------------------------------------

        /// @brief Pixel distance threshold: movement > this converts press to drag.
        static constexpr f32 kDragThreshold = 5.0f;

        Vec2f m_press_position = {0.0f, 0.0f};    ///< Where the button was pressed
        Vec2f m_current_mouse = {0.0f, 0.0f};      ///< Current mouse position

        // Drag
        Vec2f m_mouse_drag_delta = {0.0f, 0.0f};  ///< Accumulated drag delta this frame
        Vec2f m_last_mouse_pos = {0.0f, 0.0f};    ///< Last position for delta computation
        bool m_left_button_down = false;
        bool m_mouse_dragging = false;             ///< True once threshold exceeded
        bool m_drag_threshold_exceeded = false;     ///< Sticky: once drag, always drag until release

        // Click (one-frame pulse)
        bool m_was_click = false;                  ///< True for exactly one frame
        Vec2f m_click_position = {0.0f, 0.0f};    ///< Position of the click (release pos)

        // Scroll
        f32 m_scroll_delta = 0.0f;

        // Keyboard
        std::unordered_set<SDL_Scancode> m_keys_pressed;    ///< This frame only
        std::unordered_set<SDL_Scancode> m_keys_held;       ///< While held down

        // -----------------------------------------------------------------
        // Cursor
        // -----------------------------------------------------------------
        CursorStyle m_current_cursor = CursorStyle::Arrow;
        SDL_Cursor* m_cursor_arrow    = nullptr;
        SDL_Cursor* m_cursor_hand     = nullptr;
        SDL_Cursor* m_cursor_crosshair = nullptr;
        SDL_Cursor* m_cursor_sizeall  = nullptr;
    };

} // namespace parallax::core