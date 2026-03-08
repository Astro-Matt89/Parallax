/// @file widgets.cpp
/// @brief Widget implementations: Button, ToggleButton, Slider.
///
/// SPRINT 05 Task 5.2

#include "ui/widgets.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace parallax::ui
{

// =================================================================
// Button — Helpers
// =================================================================

Vec2f Button::pixel_to_ndc(Vec2f px, Vec2f viewport)
{
    return {
        (px.x / viewport.x) * 2.0f - 1.0f,
        (px.y / viewport.y) * 2.0f - 1.0f
    };
}

// =================================================================
// Button
// =================================================================

Button::Button(const std::string& label, Vec2f position, Vec2f size,
               std::function<void()> on_click)
    : m_label{label}
    , m_position{position}
    , m_size{size}
    , m_on_click{std::move(on_click)}
{
}

void Button::update(Vec2f mouse_pos, bool mouse_clicked, f32 dt)
{
    m_hovered = contains(mouse_pos);

    // Advance flash timer
    if (m_flash_timer > 0.0f)
    {
        m_flash_timer -= dt;
        if (m_flash_timer < 0.0f)
        {
            m_flash_timer = 0.0f;
        }
    }

    // Click detection
    if (m_hovered && mouse_clicked)
    {
        m_flash_timer = kFlashDuration;
        if (m_on_click)
        {
            m_on_click();
        }
    }
}

void Button::render(BitmapFont& font, rendering::LineRenderer& lines,
                    Vec2f viewport_size) const
{
    const bool flashing = m_flash_timer > 0.0f;

    // Choose colors based on state
    Vec4f border_color = m_hovered ? widget_colors::kBorderBright : widget_colors::kBorder;
    Vec3f text_color = m_hovered ? widget_colors::kTextBright : widget_colors::kTextDim;

    if (flashing)
    {
        border_color = widget_colors::kBorderBright;
        text_color = widget_colors::kTextBright;
    }

    // Convert corners to NDC for LineRenderer
    const Vec2f tl_ndc = pixel_to_ndc(m_position, viewport_size);
    const Vec2f tr_ndc = pixel_to_ndc({m_position.x + m_size.x, m_position.y}, viewport_size);
    const Vec2f br_ndc = pixel_to_ndc({m_position.x + m_size.x, m_position.y + m_size.y},
                                      viewport_size);
    const Vec2f bl_ndc = pixel_to_ndc({m_position.x, m_position.y + m_size.y}, viewport_size);

    // Highlight fill on hover (draw as two triangles via 4 line segments for visual approx)
    // Note: LineRenderer only draws lines; hover highlight is a visual cue via brighter border.
    // The PanelSystem's rect pipeline could also be used, but for widgets we keep it simple.

    // Border rectangle (4 line segments)
    lines.add_line(tl_ndc, tr_ndc, border_color);
    lines.add_line(tr_ndc, br_ndc, border_color);
    lines.add_line(br_ndc, bl_ndc, border_color);
    lines.add_line(bl_ndc, tl_ndc, border_color);

    // Flash fill: draw interior horizontal lines to approximate a filled rect
    if (flashing)
    {
        const f32 alpha = m_flash_timer / kFlashDuration;
        Vec4f flash_color = widget_colors::kFlash;
        flash_color.a *= alpha;

        // Draw a few horizontal lines spaced 2px apart to fill the rect
        for (f32 y = m_position.y + 2.0f; y < m_position.y + m_size.y - 1.0f; y += 2.0f)
        {
            const Vec2f left = pixel_to_ndc({m_position.x + 1.0f, y}, viewport_size);
            const Vec2f right = pixel_to_ndc({m_position.x + m_size.x - 1.0f, y}, viewport_size);
            lines.add_line(left, right, flash_color);
        }
    }

    // Highlight fill on hover: same approach with horizontal lines
    if (m_hovered && !flashing)
    {
        for (f32 y = m_position.y + 2.0f; y < m_position.y + m_size.y - 1.0f; y += 2.0f)
        {
            const Vec2f left = pixel_to_ndc({m_position.x + 1.0f, y}, viewport_size);
            const Vec2f right = pixel_to_ndc({m_position.x + m_size.x - 1.0f, y}, viewport_size);
            lines.add_line(left, right, widget_colors::kHighlight);
        }
    }

    // Centered text label
    const f32 char_w = 8.0f;   // BitmapFont native glyph width
    const f32 char_h = 16.0f;  // BitmapFont native glyph height
    const f32 text_w = static_cast<f32>(m_label.size()) * char_w;
    const f32 text_x = m_position.x + (m_size.x - text_w) * 0.5f;
    const f32 text_y = m_position.y + (m_size.y - char_h) * 0.5f;

    font.draw_text(m_label, text_x, text_y, 1.0f, text_color);
}

bool Button::is_hovered() const { return m_hovered; }
bool Button::is_flashing() const { return m_flash_timer > 0.0f; }

bool Button::contains(Vec2f point) const
{
    return point.x >= m_position.x &&
           point.x <= m_position.x + m_size.x &&
           point.y >= m_position.y &&
           point.y <= m_position.y + m_size.y;
}

void Button::set_position(Vec2f pos) { m_position = pos; }
void Button::set_label(const std::string& label) { m_label = label; }
Vec2f Button::get_position() const { return m_position; }
Vec2f Button::get_size() const { return m_size; }
const std::string& Button::get_label() const { return m_label; }

// =================================================================
// ToggleButton
// =================================================================

ToggleButton::ToggleButton(const std::string& label, Vec2f position, Vec2f size,
                           std::function<void()> on_click)
    : Button(label, position, size, std::move(on_click))
{
}

void ToggleButton::update(Vec2f mouse_pos, bool mouse_clicked, f32 dt)
{
    m_hovered = contains(mouse_pos);

    // Advance flash timer
    if (m_flash_timer > 0.0f)
    {
        m_flash_timer -= dt;
        if (m_flash_timer < 0.0f)
        {
            m_flash_timer = 0.0f;
        }
    }

    // Click → toggle state, then invoke callback
    if (m_hovered && mouse_clicked)
    {
        m_active = !m_active;
        m_flash_timer = kFlashDuration;
        if (m_on_click)
        {
            m_on_click();
        }
    }
}

void ToggleButton::render(BitmapFont& font, rendering::LineRenderer& lines,
                          Vec2f viewport_size) const
{
    const bool flashing = m_flash_timer > 0.0f;

    // Active state determines base colors
    Vec4f border_color;
    Vec3f text_color;

    if (m_active)
    {
        border_color = widget_colors::kBorderBright;
        text_color = widget_colors::kTextBright;
    }
    else
    {
        border_color = widget_colors::kBorder;
        text_color = widget_colors::kTextDim;
    }

    // Hover brightens inactive buttons
    if (m_hovered && !m_active)
    {
        border_color = widget_colors::kBorderBright;
        text_color = widget_colors::kTextBright;
    }

    if (flashing)
    {
        border_color = widget_colors::kBorderBright;
        text_color = widget_colors::kTextBright;
    }

    // Convert corners to NDC
    const Vec2f tl_ndc = pixel_to_ndc(m_position, viewport_size);
    const Vec2f tr_ndc = pixel_to_ndc({m_position.x + m_size.x, m_position.y}, viewport_size);
    const Vec2f br_ndc = pixel_to_ndc({m_position.x + m_size.x, m_position.y + m_size.y},
                                      viewport_size);
    const Vec2f bl_ndc = pixel_to_ndc({m_position.x, m_position.y + m_size.y}, viewport_size);

    // Border rectangle
    lines.add_line(tl_ndc, tr_ndc, border_color);
    lines.add_line(tr_ndc, br_ndc, border_color);
    lines.add_line(br_ndc, bl_ndc, border_color);
    lines.add_line(bl_ndc, tl_ndc, border_color);

    // Active fill: horizontal line fill (filled background when ON)
    if (m_active)
    {
        for (f32 y = m_position.y + 2.0f; y < m_position.y + m_size.y - 1.0f; y += 2.0f)
        {
            const Vec2f left = pixel_to_ndc({m_position.x + 1.0f, y}, viewport_size);
            const Vec2f right = pixel_to_ndc({m_position.x + m_size.x - 1.0f, y},
                                             viewport_size);
            lines.add_line(left, right, widget_colors::kActiveFill);
        }
    }

    // Flash overlay
    if (flashing)
    {
        const f32 alpha = m_flash_timer / kFlashDuration;
        Vec4f flash_color = widget_colors::kFlash;
        flash_color.a *= alpha;

        for (f32 y = m_position.y + 2.0f; y < m_position.y + m_size.y - 1.0f; y += 2.0f)
        {
            const Vec2f left = pixel_to_ndc({m_position.x + 1.0f, y}, viewport_size);
            const Vec2f right = pixel_to_ndc({m_position.x + m_size.x - 1.0f, y},
                                             viewport_size);
            lines.add_line(left, right, flash_color);
        }
    }

    // Hover highlight on inactive
    if (m_hovered && !m_active && !flashing)
    {
        for (f32 y = m_position.y + 2.0f; y < m_position.y + m_size.y - 1.0f; y += 2.0f)
        {
            const Vec2f left = pixel_to_ndc({m_position.x + 1.0f, y}, viewport_size);
            const Vec2f right = pixel_to_ndc({m_position.x + m_size.x - 1.0f, y},
                                             viewport_size);
            lines.add_line(left, right, widget_colors::kHighlight);
        }
    }

    // Centered text label
    const f32 char_w = 8.0f;
    const f32 char_h = 16.0f;
    const f32 text_w = static_cast<f32>(m_label.size()) * char_w;
    const f32 text_x = m_position.x + (m_size.x - text_w) * 0.5f;
    const f32 text_y = m_position.y + (m_size.y - char_h) * 0.5f;

    font.draw_text(m_label, text_x, text_y, 1.0f, text_color);
}

bool ToggleButton::is_active() const { return m_active; }
void ToggleButton::set_active(bool active) { m_active = active; }
void ToggleButton::toggle() { m_active = !m_active; }

// =================================================================
// Slider — Helpers
// =================================================================

Vec2f Slider::pixel_to_ndc(Vec2f px, Vec2f viewport)
{
    return {
        (px.x / viewport.x) * 2.0f - 1.0f,
        (px.y / viewport.y) * 2.0f - 1.0f
    };
}

// =================================================================
// Slider
// =================================================================

Slider::Slider(const std::string& label, Vec2f position, f32 width,
               f32 min_val, f32 max_val, f32 step)
    : m_label{label}
    , m_position{position}
    , m_width{width}
    , m_value{min_val}
    , m_min{min_val}
    , m_max{max_val}
    , m_step{step}
{
    compute_track_rect();
}

void Slider::compute_track_rect()
{
    // Layout: [label text] [track] [value text]
    const f32 char_w = 8.0f;
    const f32 label_w = static_cast<f32>(m_label.size()) * char_w + 8.0f;  // + padding

    m_track_x = m_position.x + label_w;
    m_track_width = m_width - label_w - kValueWidth;
    m_track_y = m_position.y + kSliderHeight * 0.5f;

    // Ensure minimum track width
    if (m_track_width < 20.0f)
    {
        m_track_width = 20.0f;
    }
}

f32 Slider::snap_value(f32 val) const
{
    if (m_step <= 0.0f)
    {
        return std::clamp(val, m_min, m_max);
    }

    val = std::clamp(val, m_min, m_max);
    const f32 offset = val - m_min;
    const f32 snapped = m_min + std::round(offset / m_step) * m_step;
    return std::clamp(snapped, m_min, m_max);
}

void Slider::update(Vec2f mouse_pos, bool mouse_down, bool mouse_clicked, f32 /*dt*/)
{
    const f32 handle_x = m_track_x + ((m_value - m_min) / (m_max - m_min)) * m_track_width;

    // Hit test on track area (expanded vertically for easier clicking)
    const bool over_track = mouse_pos.x >= m_track_x - kHandleRadius &&
                            mouse_pos.x <= m_track_x + m_track_width + kHandleRadius &&
                            mouse_pos.y >= m_track_y - kSliderHeight * 0.5f &&
                            mouse_pos.y <= m_track_y + kSliderHeight * 0.5f;

    // Hit test on handle specifically
    const f32 dx = mouse_pos.x - handle_x;
    const f32 dy = mouse_pos.y - m_track_y;
    const bool over_handle = (dx * dx + dy * dy) <= (kHandleRadius + 4.0f) * (kHandleRadius + 4.0f);

    m_hovered = over_track || over_handle;

    // Start drag on click over track or handle
    if (mouse_clicked && m_hovered)
    {
        m_dragging = true;
    }

    // Continue drag as long as mouse is held
    if (m_dragging)
    {
        if (!mouse_down)
        {
            m_dragging = false;
        }
        else
        {
            // Map mouse X to value
            const f32 t = std::clamp(
                (mouse_pos.x - m_track_x) / m_track_width, 0.0f, 1.0f);
            const f32 new_val = snap_value(m_min + t * (m_max - m_min));

            if (new_val != m_value)
            {
                m_value = new_val;
                if (on_value_changed)
                {
                    on_value_changed(m_value);
                }
            }
        }
    }
}

void Slider::render(BitmapFont& font, rendering::LineRenderer& lines,
                    Vec2f viewport_size) const
{
    const Vec4f track_color = m_hovered ? widget_colors::kBorder : widget_colors::kTrack;
    const Vec4f handle_color = m_dragging ? widget_colors::kFlash : widget_colors::kHandle;

    // Track line (horizontal)
    const Vec2f track_left = pixel_to_ndc({m_track_x, m_track_y}, viewport_size);
    const Vec2f track_right = pixel_to_ndc({m_track_x + m_track_width, m_track_y},
                                           viewport_size);
    lines.add_line(track_left, track_right, track_color);

    // Draw a second line 1px below for slightly thicker track appearance
    const Vec2f track_left2 = pixel_to_ndc({m_track_x, m_track_y + 1.0f}, viewport_size);
    const Vec2f track_right2 = pixel_to_ndc({m_track_x + m_track_width, m_track_y + 1.0f},
                                            viewport_size);
    lines.add_line(track_left2, track_right2, track_color);

    // End caps (small vertical ticks at track endpoints)
    const f32 cap_h = 4.0f;
    lines.add_line(pixel_to_ndc({m_track_x, m_track_y - cap_h}, viewport_size),
                   pixel_to_ndc({m_track_x, m_track_y + cap_h}, viewport_size),
                   track_color);
    lines.add_line(pixel_to_ndc({m_track_x + m_track_width, m_track_y - cap_h}, viewport_size),
                   pixel_to_ndc({m_track_x + m_track_width, m_track_y + cap_h}, viewport_size),
                   track_color);

    // Handle (small circle or diamond at current value position)
    const f32 t = (m_max > m_min) ? (m_value - m_min) / (m_max - m_min) : 0.0f;
    const f32 handle_x = m_track_x + t * m_track_width;

    // Draw handle as a small diamond (4 line segments)
    const f32 r = kHandleRadius;
    const Vec2f top    = pixel_to_ndc({handle_x, m_track_y - r}, viewport_size);
    const Vec2f right  = pixel_to_ndc({handle_x + r, m_track_y}, viewport_size);
    const Vec2f bottom = pixel_to_ndc({handle_x, m_track_y + r}, viewport_size);
    const Vec2f left   = pixel_to_ndc({handle_x - r, m_track_y}, viewport_size);

    lines.add_line(top, right, handle_color);
    lines.add_line(right, bottom, handle_color);
    lines.add_line(bottom, left, handle_color);
    lines.add_line(left, top, handle_color);

    // Fill lines inside diamond for visibility when dragging
    if (m_dragging || m_hovered)
    {
        for (f32 dy = -r + 1.0f; dy < r; dy += 2.0f)
        {
            const f32 half_w = r - std::abs(dy);
            if (half_w < 1.0f)
            {
                continue;
            }
            const Vec2f l = pixel_to_ndc({handle_x - half_w, m_track_y + dy}, viewport_size);
            const Vec2f rr = pixel_to_ndc({handle_x + half_w, m_track_y + dy}, viewport_size);
            Vec4f fill = handle_color;
            fill.a *= 0.5f;
            lines.add_line(l, rr, fill);
        }
    }

    // Label text (left of track)
    const f32 label_y = m_position.y + (kSliderHeight - 16.0f) * 0.5f;
    font.draw_text(m_label, m_position.x, label_y, 1.0f,
                   m_hovered ? widget_colors::kTextBright : widget_colors::kTextDim);

    // Value text (right of track)
    const std::string value_str = std::vformat(m_format, std::make_format_args(m_value));
    const f32 value_x = m_track_x + m_track_width + 8.0f;
    font.draw_text(value_str, value_x, label_y, 1.0f, widget_colors::kTextBright);
}

f32 Slider::get_value() const { return m_value; }

void Slider::set_value(f32 val)
{
    m_value = snap_value(val);
}

bool Slider::contains(Vec2f point) const
{
    return point.x >= m_track_x - kHandleRadius &&
           point.x <= m_track_x + m_track_width + kHandleRadius &&
           point.y >= m_position.y &&
           point.y <= m_position.y + kSliderHeight;
}

bool Slider::is_dragging() const { return m_dragging; }

void Slider::set_position(Vec2f pos)
{
    m_position = pos;
    compute_track_rect();
}

void Slider::set_format_string(const std::string& fmt) { m_format = fmt; }
Vec2f Slider::get_position() const { return m_position; }
f32 Slider::get_width() const { return m_width; }

} // namespace parallax::ui