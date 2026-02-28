#pragma once

/// @file input.hpp
/// @brief SDL2 input state tracker: mouse drag, scroll, keyboard.
///
/// Input only tracks state — it does NOT modify the camera or any other system.
/// The Application loop reads Input state and translates it to Camera/simulation actions.

#include "core/types.hpp"

#include <SDL2/SDL.h>

#include <unordered_set>

namespace parallax::core
{
    /// @brief Tracks per-frame input state from SDL2 events.
    ///
    /// Usage pattern each frame:
    ///   1. Call new_frame() to reset per-frame deltas
    ///   2. For each SDL_Event from Window::poll_events(), call process_event()
    ///   3. Query state: is_mouse_dragging(), get_mouse_drag_delta(), get_scroll_delta(), etc.
    ///   4. Application translates state to Camera::pan(), Camera::zoom(), etc.
    class Input
    {
    public:
        Input() = default;
        ~Input() = default;

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
        // Mouse state
        // -----------------------------------------------------------------

        /// @brief True if the left mouse button is held and the mouse has moved.
        [[nodiscard]] bool is_mouse_dragging() const;

        /// @brief Accumulated mouse drag delta this frame in pixels (x, y).
        /// Positive x = rightward, positive y = downward (SDL screen coords).
        [[nodiscard]] Vec2f get_mouse_drag_delta() const;

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

    private:
        // Mouse
        Vec2f m_mouse_drag_delta = {0.0f, 0.0f};
        f32 m_scroll_delta = 0.0f;
        bool m_mouse_dragging = false;
        bool m_left_button_down = false;
        Vec2f m_last_mouse_pos = {0.0f, 0.0f};

        // Keyboard
        std::unordered_set<SDL_Scancode> m_keys_pressed;    ///< This frame only
        std::unordered_set<SDL_Scancode> m_keys_held;       ///< While held down
    };

} // namespace parallax::core