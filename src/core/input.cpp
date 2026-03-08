/// @file input.cpp
/// @brief SDL2 input state tracker implementation.
///
/// SPRINT 05 Task 5.6: Click vs drag, cursor management.

#include "core/input.hpp"

#include <cmath>

namespace parallax::core
{

// =================================================================
// Constructor / Destructor — create and destroy SDL cursors
// =================================================================

Input::Input()
{
    m_cursor_arrow     = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    m_cursor_hand      = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    m_cursor_crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    m_cursor_sizeall   = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
}

Input::~Input()
{
    if (m_cursor_arrow)     SDL_FreeCursor(m_cursor_arrow);
    if (m_cursor_hand)      SDL_FreeCursor(m_cursor_hand);
    if (m_cursor_crosshair) SDL_FreeCursor(m_cursor_crosshair);
    if (m_cursor_sizeall)   SDL_FreeCursor(m_cursor_sizeall);
}

// =================================================================
// new_frame — reset per-frame deltas
// =================================================================

void Input::new_frame()
{
    m_mouse_drag_delta = {0.0f, 0.0f};
    m_scroll_delta = 0.0f;
    m_was_click = false;
    m_keys_pressed.clear();
}

// =================================================================
// process_event — dispatch SDL events to state tracking
// =================================================================

void Input::process_event(const SDL_Event& event)
{
    switch (event.type)
    {
        // -----------------------------------------------------------------
        // Mouse button down — record press position, start tracking
        // -----------------------------------------------------------------
        case SDL_MOUSEBUTTONDOWN:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                m_left_button_down = true;
                m_drag_threshold_exceeded = false;
                m_mouse_dragging = false;

                m_press_position = {
                    static_cast<f32>(event.button.x),
                    static_cast<f32>(event.button.y)
                };
                m_last_mouse_pos = m_press_position;
            }
            break;
        }

        // -----------------------------------------------------------------
        // Mouse button up — determine click or end drag
        // -----------------------------------------------------------------
        case SDL_MOUSEBUTTONUP:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                if (m_left_button_down && !m_drag_threshold_exceeded)
                {
                    // Released within threshold → this is a click
                    m_was_click = true;
                    m_click_position = {
                        static_cast<f32>(event.button.x),
                        static_cast<f32>(event.button.y)
                    };
                }

                m_left_button_down = false;
                m_mouse_dragging = false;
                m_drag_threshold_exceeded = false;
            }
            break;
        }

        // -----------------------------------------------------------------
        // Mouse motion — update position, check drag threshold, accumulate delta
        // -----------------------------------------------------------------
        case SDL_MOUSEMOTION:
        {
            m_current_mouse = {
                static_cast<f32>(event.motion.x),
                static_cast<f32>(event.motion.y)
            };

            if (m_left_button_down)
            {
                const Vec2f current_pos = m_current_mouse;

                // Check if we've crossed the drag threshold
                if (!m_drag_threshold_exceeded)
                {
                    const f32 dx = current_pos.x - m_press_position.x;
                    const f32 dy = current_pos.y - m_press_position.y;
                    const f32 dist = std::sqrt(dx * dx + dy * dy);

                    if (dist > kDragThreshold)
                    {
                        m_drag_threshold_exceeded = true;
                        m_mouse_dragging = true;
                        // Reset last_mouse_pos to current to avoid a jump
                        // on the first frame of dragging
                        m_last_mouse_pos = current_pos;
                    }
                }

                // Accumulate drag delta only once threshold is exceeded
                if (m_drag_threshold_exceeded)
                {
                    m_mouse_drag_delta.x += current_pos.x - m_last_mouse_pos.x;
                    m_mouse_drag_delta.y += current_pos.y - m_last_mouse_pos.y;
                    m_last_mouse_pos = current_pos;
                    m_mouse_dragging = true;
                }
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

// =================================================================
// Mouse queries — click vs drag
// =================================================================

bool Input::was_click() const
{
    return m_was_click;
}

Vec2f Input::get_click_position() const
{
    return m_click_position;
}

bool Input::is_mouse_dragging() const
{
    return m_mouse_dragging;
}

Vec2f Input::get_mouse_drag_delta() const
{
    return m_mouse_drag_delta;
}

bool Input::is_left_button_down() const
{
    return m_left_button_down;
}

// =================================================================
// Mouse position
// =================================================================

Vec2f Input::get_mouse_position() const
{
    return m_current_mouse;
}

// =================================================================
// Scroll
// =================================================================

f32 Input::get_scroll_delta() const
{
    return m_scroll_delta;
}

// =================================================================
// Keyboard queries
// =================================================================

bool Input::is_key_pressed(SDL_Scancode key) const
{
    return m_keys_pressed.contains(key);
}

bool Input::is_key_held(SDL_Scancode key) const
{
    return m_keys_held.contains(key);
}

// =================================================================
// Cursor management
// =================================================================

void Input::set_cursor(CursorStyle style)
{
    if (style == m_current_cursor)
    {
        return;
    }

    m_current_cursor = style;

    switch (style)
    {
        case CursorStyle::Arrow:
            if (m_cursor_arrow) SDL_SetCursor(m_cursor_arrow);
            break;
        case CursorStyle::Hand:
            if (m_cursor_hand) SDL_SetCursor(m_cursor_hand);
            break;
        case CursorStyle::Crosshair:
            if (m_cursor_crosshair) SDL_SetCursor(m_cursor_crosshair);
            break;
        case CursorStyle::SizeAll:
            if (m_cursor_sizeall) SDL_SetCursor(m_cursor_sizeall);
            break;
    }
}

CursorStyle Input::get_cursor_style() const
{
    return m_current_cursor;
}

} // namespace parallax::core