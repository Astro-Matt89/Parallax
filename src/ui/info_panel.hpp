#pragma once

/// @file info_panel.hpp
/// @brief Right-side information panel for the currently selected object.
///
/// Appears when an object is selected; auto-hides when selection is cleared.
/// Slides in from the right edge (same auto-show/hide pattern as Toolbar/SidePanel).
///
/// Displays:
///   - Object name / designation
///   - RA/Dec (formatted)
///   - Alt/Az (formatted)
///   - Magnitude, B-V, spectral type, constellation
///   - DSO: type, angular size
///   - TRACK button: camera follows object
///   - GOTO button: placeholder for telescope control
///
/// SPRINT 05 Task 5.5

#include "core/types.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/selection.hpp"
#include "ui/widgets.hpp"

#include <functional>
#include <memory>
#include <string>

namespace parallax::ui
{

// =================================================================
// InfoPanelCallbacks — actions the panel invokes on Application
// =================================================================

/// @brief Callbacks from the InfoPanel back to the Application.
struct InfoPanelCallbacks
{
    /// @brief Enable tracking mode for the currently selected object.
    std::function<void()> track;

    /// @brief GOTO placeholder — future telescope slew command.
    std::function<void()> goto_object;
};

// =================================================================
// InfoPanel
// =================================================================

/// @brief Right-side panel showing selected object information.
///
/// Behavior:
///   - Automatically visible when Selection::has_selection() is true.
///   - Slides in from the right screen edge.
///   - Slides out when selection is cleared.
///   - TRACK and GOTO buttons at the bottom.
class InfoPanel
{
public:
    InfoPanel() = default;
    ~InfoPanel() = default;

    InfoPanel(const InfoPanel&) = delete;
    InfoPanel& operator=(const InfoPanel&) = delete;
    InfoPanel(InfoPanel&&) = delete;
    InfoPanel& operator=(InfoPanel&&) = delete;

    /// @brief Initialize widgets.
    /// @param callbacks Actions for TRACK/GOTO buttons.
    void init(const InfoPanelCallbacks& callbacks);

    /// @brief Update panel state from current selection.
    /// @param selection  Current selection system state.
    /// @param mouse_pos  Current mouse position in screen pixels.
    /// @param mouse_clicked True if left mouse button was clicked this frame.
    /// @param dt         Delta time in seconds.
    /// @param viewport_width  Current viewport width.
    /// @param viewport_height Current viewport height.
    void update(const Selection& selection,
                Vec2f mouse_pos, bool mouse_clicked, f32 dt,
                u32 viewport_width, u32 viewport_height);

    /// @brief Render the panel, labels, and buttons.
    void render(BitmapFont& font, rendering::LineRenderer& lines,
                VkExtent2D extent) const;

    /// @brief True if the panel is currently visible.
    [[nodiscard]] bool is_visible() const;

    /// @brief True if the mouse is over the panel area.
    [[nodiscard]] bool is_mouse_over(Vec2f mouse_pos) const;

private:
    void layout_widgets(u32 viewport_width, u32 viewport_height);

    /// @brief Format RA in hours, minutes, seconds.
    [[nodiscard]] static std::string format_ra(f64 ra_rad);

    /// @brief Format Dec in degrees, arcmin, arcsec.
    [[nodiscard]] static std::string format_dec(f64 dec_rad);

    /// @brief Format Alt in degrees.
    [[nodiscard]] static std::string format_alt(f64 alt_rad);

    /// @brief Format Az in degrees.
    [[nodiscard]] static std::string format_az(f64 az_rad);

    // -----------------------------------------------------------------
    // Visibility / animation
    // -----------------------------------------------------------------
    static constexpr f32 kPanelWidth  = 280.0f;
    static constexpr f32 kPanelHeight = 400.0f;
    static constexpr f32 kSlideSpeed  = 8.0f;
    static constexpr f32 kPadding     = 10.0f;
    static constexpr f32 kRowHeight   = 18.0f;

    f32 m_slide_t = 0.0f;      ///< 0 = hidden (off right), 1 = fully visible
    bool m_initialized = false;
    bool m_should_show = false;

    // -----------------------------------------------------------------
    // Layout
    // -----------------------------------------------------------------
    f32 m_panel_x = 0.0f;
    f32 m_panel_y = 0.0f;
    u32 m_viewport_w = 0;
    u32 m_viewport_h = 0;

    // -----------------------------------------------------------------
    // Widgets
    // -----------------------------------------------------------------
    std::unique_ptr<Button> m_btn_track;
    std::unique_ptr<Button> m_btn_goto;

    // -----------------------------------------------------------------
    // Display text (rebuilt each frame from SelectedObject)
    // -----------------------------------------------------------------
    std::string m_title;            ///< Object name or "HIP NNNNN"
    std::string m_subtitle;         ///< Bayer designation or DSO designation
    std::string m_type_text;        ///< "Star" or DSO type name
    std::string m_ra_text;
    std::string m_dec_text;
    std::string m_alt_text;
    std::string m_az_text;
    std::string m_mag_text;
    std::string m_bv_text;
    std::string m_spectral_text;
    std::string m_constellation_text;
    std::string m_size_text;        ///< DSO angular size / Solar System angular diameter
    std::string m_dist_text;        ///< Solar System body distance (AU or km for Moon)
    std::string m_phase_text;       ///< Solar System phase angle
    std::string m_illum_text;       ///< Solar System illumination fraction
    std::string m_tracking_text;    ///< "TRACKING" or empty

    /// @brief Currently selected type (for conditional display logic).
    SelectedObjectType m_display_type = SelectedObjectType::None;
};

} // namespace parallax::ui