#pragma once

/// @file side_panel.hpp
/// @brief Left side panel for observer location, Bortle scale, magnitude limit, time speed.
///
/// Appears when the mouse moves to the left screen edge, slides in from left.
/// Contains three sections separated by horizontal rules:
///   1. OBSERVER — location presets + lat/lon/elevation readout
///   2. TIME     — UTC, LST, JD display + speed buttons
///   3. SKY      — Bortle scale buttons (1–9) + magnitude limit slider
///
/// Uses the same auto-show/hide pattern as the bottom Toolbar (Task 5.3).
///
/// SPRINT 05 Task 5.4

#include "astro/coordinates.hpp"
#include "core/types.hpp"
#include "rendering/line_renderer.hpp"
#include "rendering/sky_background.hpp"
#include "ui/font.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace parallax::ui
{

// =================================================================
// ObserverPreset — a named observatory location
// =================================================================

/// @brief A named observer site with coordinates and default sky quality.
struct ObserverPreset
{
    std::string name;
    f64 latitude_deg;       ///< Geographic latitude (north positive)
    f64 longitude_deg;      ///< Geographic longitude (east positive)
    f64 elevation_m;        ///< Elevation above sea level in meters
    f32 default_bortle;     ///< Default Bortle scale for this site
};

// =================================================================
// SidePanelState — snapshot of application state for readout display
// =================================================================

/// @brief Snapshot of application state for the side panel readout lines.
///
/// The Application fills this each frame so the panel can display
/// current observer, time, and sky information without coupling
/// to Application internals.
struct SidePanelState
{
    // Observer
    f64 latitude_deg    = 0.0;
    f64 longitude_deg   = 0.0;
    f64 elevation_m     = 0.0;
    i32 selected_preset = -1;       ///< Index into presets, -1 = custom

    // Time
    std::string utc_string;         ///< e.g. "2026-03-01 19:14:55"
    std::string lst_string;         ///< e.g. "04h 41m 47s"
    f64 julian_date     = 0.0;
    f64 time_scale      = 1.0;
    bool time_paused    = false;

    // Sky
    f32 bortle_scale    = 4.0f;
    f32 magnitude_limit = 6.5f;
};

// =================================================================
// SidePanelCallbacks — actions the panel can invoke on Application
// =================================================================

/// @brief Callbacks from the side panel back to the Application.
struct SidePanelCallbacks
{
    /// @brief Set observer location. Application must update observer coords AND rotate sky.
    /// @param lat_deg  Latitude in degrees (north positive).
    /// @param lon_deg  Longitude in degrees (east positive).
    /// @param elev_m   Elevation in meters.
    /// @param bortle   Default Bortle scale for this site.
    std::function<void(f64 lat_deg, f64 lon_deg, f64 elev_m, f32 bortle)> set_location;

    /// @brief Set Bortle scale. Application must update sky background parameters.
    std::function<void(f32 bortle)> set_bortle;

    /// @brief Set magnitude limit.
    std::function<void(f32 mag_limit)> set_magnitude_limit;

    /// @brief Set time speed multiplier.
    std::function<void(f64 scale)> set_time_scale;
};

// =================================================================
// SidePanel
// =================================================================

/// @brief Left side panel with observer location, Bortle, magnitude, time speed.
///
/// Behavior:
///   - Mouse enters left activation zone → panel slides in from left.
///   - Mouse leaves panel area → panel slides out.
///   - Buttons and sliders are interactive while visible.
///   - Location presets are toggle-button rows; selecting one calls set_location.
///   - Bortle buttons are a 1–9 grid; selecting one calls set_bortle.
///   - Magnitude slider calls set_magnitude_limit on drag.
///   - Time speed buttons call set_time_scale.
class SidePanel
{
public:
    SidePanel() = default;
    ~SidePanel() = default;

    SidePanel(const SidePanel&) = delete;
    SidePanel& operator=(const SidePanel&) = delete;
    SidePanel(SidePanel&&) = delete;
    SidePanel& operator=(SidePanel&&) = delete;

    /// @brief Initialize all widgets and load built-in presets.
    /// @param callbacks Actions for each button/slider.
    void init(const SidePanelCallbacks& callbacks);

    /// @brief Sync state and update hover/animation/input.
    /// @param mouse_pos      Current mouse position in screen pixels.
    /// @param mouse_clicked  True if the left mouse button was clicked this frame.
    /// @param mouse_down     True if the left mouse button is currently held.
    /// @param dt             Delta time in seconds.
    /// @param viewport_width  Current viewport width in pixels.
    /// @param viewport_height Current viewport height in pixels.
    /// @param state          Current application state for display.
    void update(Vec2f mouse_pos, bool mouse_clicked, bool mouse_down, f32 dt,
                u32 viewport_width, u32 viewport_height,
                const SidePanelState& state);

    /// @brief Render panel background, labels, widgets, separators.
    /// @param font  BitmapFont for text rendering.
    /// @param lines LineRenderer for borders and separators.
    void render(BitmapFont& font, rendering::LineRenderer& lines,
                VkExtent2D extent) const;

    /// @brief True if the panel is currently visible (fully or partially).
    [[nodiscard]] bool is_visible() const;

    /// @brief True if the mouse is over the panel area (for blocking sky interaction).
    [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

    /// @brief True if any slider in the panel is being dragged.
    [[nodiscard]] bool is_dragging() const;

    /// @brief Get the list of built-in observer presets.
    [[nodiscard]] const std::vector<ObserverPreset>& get_presets() const;

private:
    void create_location_group(const SidePanelCallbacks& callbacks);
    void create_bortle_group(const SidePanelCallbacks& callbacks);
    void create_sky_group(const SidePanelCallbacks& callbacks);
    void create_time_speed_group(const SidePanelCallbacks& callbacks);

    void layout_widgets(u32 viewport_width, u32 viewport_height);

    /// @brief Format latitude as ±DD° MM' SS" N/S.
    [[nodiscard]] static std::string format_latitude(f64 lat_deg);

    /// @brief Format longitude as ±DDD° MM' SS" E/W.
    [[nodiscard]] static std::string format_longitude(f64 lon_deg);

    // -----------------------------------------------------------------
    // Visibility / animation (same pattern as Toolbar)
    // -----------------------------------------------------------------

    /// @brief Width of the activation zone at the left screen edge (pixels).
    static constexpr f32 kActivationZone = 30.0f;

    /// @brief Total panel width (pixels).
    static constexpr f32 kPanelWidth = 260.0f;

    /// @brief Panel height (pixels) — enough for all content.
    static constexpr f32 kPanelHeight = 560.0f;

    /// @brief Slide animation speed (0→1 in seconds).
    static constexpr f32 kSlideSpeed = 6.0f;

    /// @brief Linger time after mouse leaves before hiding (seconds).
    static constexpr f32 kLingerTime = 0.5f;

    f32 m_slide_t = 0.0f;      ///< 0 = hidden (off left), 1 = fully visible
    f32 m_linger_timer = 0.0f;
    bool m_mouse_in_zone = false;
    bool m_initialized = false;

    // -----------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------
    f32 m_panel_x = 0.0f;      ///< Current left edge X (animated, negative when hidden)
    f32 m_panel_y = 0.0f;      ///< Top edge Y (vertically centered)
    u32 m_viewport_w = 0;
    u32 m_viewport_h = 0;

    /// @brief Inner content padding (pixels).
    static constexpr f32 kPadding = 10.0f;

    /// @brief Row height for label lines (pixels).
    static constexpr f32 kRowHeight = 20.0f;

    /// @brief Button size for small grid buttons (Bortle, speed).
    static constexpr f32 kSmallBtnW = 32.0f;
    static constexpr f32 kSmallBtnH = 24.0f;
    static constexpr f32 kSmallBtnSpacing = 4.0f;

    /// @brief Button size for location preset buttons.
    static constexpr f32 kPresetBtnW = 230.0f;
    static constexpr f32 kPresetBtnH = 22.0f;

    // -----------------------------------------------------------------
    // Presets
    // -----------------------------------------------------------------
    std::vector<ObserverPreset> m_presets;
    i32 m_selected_preset = 0;

    // -----------------------------------------------------------------
    // Widgets — Location presets
    // -----------------------------------------------------------------
    std::vector<std::unique_ptr<ToggleButton>> m_preset_buttons;

    // -----------------------------------------------------------------
    // Widgets — Bortle scale (9 buttons)
    // -----------------------------------------------------------------
    std::vector<std::unique_ptr<ToggleButton>> m_bortle_buttons;
    i32 m_selected_bortle = 3;  ///< 0-indexed (Bortle 4 = index 3)

    // -----------------------------------------------------------------
    // Widgets — Magnitude limit slider
    // -----------------------------------------------------------------
    std::unique_ptr<Slider> m_slider_mag_limit;

    // -----------------------------------------------------------------
    // Widgets — Time speed buttons
    // -----------------------------------------------------------------
    std::vector<std::unique_ptr<Button>> m_speed_buttons;

    // -----------------------------------------------------------------
    // Readout text (filled each frame from SidePanelState)
    // -----------------------------------------------------------------
    std::string m_lat_text;
    std::string m_lon_text;
    std::string m_elev_text;
    std::string m_utc_text;
    std::string m_lst_text;
    std::string m_jd_text;
    std::string m_bortle_text;
};

} // namespace parallax::ui