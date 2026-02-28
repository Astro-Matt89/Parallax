/// @file input.cpp
/// @brief SDL2 input state tracker implementation.

#include "core/input.hpp"

namespace parallax::core
{

// -----------------------------------------------------------------
// new_frame — reset per-frame deltas
// -----------------------------------------------------------------

void Input::new_frame()
{
    m_mouse_drag_delta = {0.0f, 0.0f};
    m_scroll_delta = 0.0f;
    m_keys_pressed.clear();
}

// -----------------------------------------------------------------
// process_event — dispatch SDL events to state tracking
// -----------------------------------------------------------------

void Input::process_event(const SDL_Event& event)
{
    switch (event.type)
    {
        // -----------------------------------------------------------------
        // Mouse button
        // -----------------------------------------------------------------
        case SDL_MOUSEBUTTONDOWN:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                m_left_button_down = true;
                m_last_mouse_pos = {
                    static_cast<f32>(event.button.x),
                    static_cast<f32>(event.button.y)
                };
            }
            break;
        }

        case SDL_MOUSEBUTTONUP:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                m_left_button_down = false;
                m_mouse_dragging = false;
            }
            break;
        }

        // -----------------------------------------------------------------
        // Mouse motion
        // -----------------------------------------------------------------
        case SDL_MOUSEMOTION:
        {
            if (m_left_button_down)
            {
                const Vec2f current_pos = {
                    static_cast<f32>(event.motion.x),
                    static_cast<f32>(event.motion.y)
                };

                // Accumulate drag delta for this frame
                m_mouse_drag_delta.x += current_pos.x - m_last_mouse_pos.x;
                m_mouse_drag_delta.y += current_pos.y - m_last_mouse_pos.y;

                m_last_mouse_pos = current_pos;
                m_mouse_dragging = true;
            }
            break;
        }

        // -----------------------------------------------------------------
        // Mouse wheel (scroll)
        // -----------------------------------------------------------------
        case SDL_MOUSEWHEEL:
        {
            // SDL: positive y = scroll up. We preserve this convention:
            // positive = zoom in, negative = zoom out.
            m_scroll_delta += static_cast<f32>(event.wheel.y);
            break;
        }

        // -----------------------------------------------------------------
        // Keyboard
        // -----------------------------------------------------------------
        case SDL_KEYDOWN:
        {
            // Ignore key-repeat events for "pressed this frame" tracking
            if (event.key.repeat == 0)
            {
                m_keys_pressed.insert(event.key.keysym.scancode);
            }
            m_keys_held.insert(event.key.keysym.scancode);
            break;
        }

        case SDL_KEYUP:
        {
            m_keys_held.erase(event.key.keysym.scancode);
            break;
        }

        default:
            break;
    }
}

// -----------------------------------------------------------------
// Mouse queries
// -----------------------------------------------------------------

bool Input::is_mouse_dragging() const
{
    return m_mouse_dragging;
}

Vec2f Input::get_mouse_drag_delta() const
{
    return m_mouse_drag_delta;
}

f32 Input::get_scroll_delta() const
{
    return m_scroll_delta;
}

// -----------------------------------------------------------------
// Keyboard queries
// -----------------------------------------------------------------

bool Input::is_key_pressed(SDL_Scancode key) const
{
    return m_keys_pressed.contains(key);
}

bool Input::is_key_held(SDL_Scancode key) const
{
    return m_keys_held.contains(key);
}

} // namespace parallax::core