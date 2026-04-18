#pragma once

/// @file hud.hpp
/// @brief Retro green terminal HUD overlay for the planetarium.
///
/// Five panels: time (top-left), camera (top-right),
/// observer (bottom-left), performance (bottom-right),
/// overlay status bar (bottom-center).
/// Uses the BitmapFont from Task 3.5.

#include "core/types.hpp"
#include "ui/font.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <filesystem>
#include <string>

namespace parallax::ui
{
    /// @brief Time display format for the HUD time panel.
    enum class TimeDisplayFormat : u8
    {
        kUtc = 0,   ///< Show UTC date/time
        kLst,       ///< Show Local Sidereal Time
        kJd,        ///< Show Julian Date
        kCount      ///< Sentinel for cycling
    };

    /// @brief All data the HUD needs to display each frame.
    struct HudData
    {
        // Time
        f64 julian_date = 0.0;
        f64 local_sidereal_time_rad = 0.0;
        f64 utc_hours = 0.0;

        // Camera
        f64 altitude_deg = 0.0;
        f64 azimuth_deg = 0.0;
        f64 fov_deg = 60.0;
        f32 magnitude_limit = 6.5f;

        // Observer
        f64 latitude_deg = 0.0;
        f64 longitude_deg = 0.0;
        f32 bortle_scale = 4.0f;

        // Performance
        f32 fps = 0.0f;
        u32 visible_stars = 0;
        u32 total_stars = 0;

        // Simulation
        f64 time_scale = 1.0;

        // Overlay status                                   ← SPRINT 04 Task 4.7
        bool overlay_const = true;          ///< Constellation lines visible
        const char* overlay_grid_name = "None"; ///< Grid type name string
        bool overlay_dso = true;            ///< DSO icons visible
        bool overlay_horizon = true;        ///< Horizon + cardinals visible

        // Sky state                                        ← SPRINT 06 Task 6.7
        f32 sun_altitude_deg = -90.0f;      ///< Sun altitude in degrees (from sky background)
        bool atmosphere_on = true;          ///< Atmosphere toggle state
    };

    /// @brief Retro green terminal HUD overlay.
    ///
    /// Renders five information panels over the starfield using the BitmapFont.
    /// Call update() with fresh data each frame, then render() inside the render pass.
    class Hud
    {
    public:
        /// @brief Create the HUD and its internal BitmapFont.
        /// @param context The Vulkan context.
        /// @param render_pass The render pass this will be drawn in.
        /// @param shader_dir Directory containing compiled SPIR-V files.
        Hud(const vulkan::Context& context,
            VkRenderPass render_pass,
            const std::filesystem::path& shader_dir);

        ~Hud() = default;

        Hud(const Hud&) = delete;
        Hud& operator=(const Hud&) = delete;
        Hud(Hud&&) = delete;
        Hud& operator=(Hud&&) = delete;

        /// @brief Update HUD data for this frame.
        /// @param data The complete HUD data snapshot.
        void update(const HudData& data);

        /// @brief Render the HUD overlay.
        ///
        /// Must be called inside an active render pass, AFTER sky + starfield + overlays.
        /// @param cmd The command buffer to record into.
        /// @param viewport_extent The current viewport dimensions.
        void render(VkCommandBuffer cmd, VkExtent2D viewport_extent);

        /// @brief Toggle HUD visibility on/off.
        void toggle_visible();

        /// @brief Check if the HUD is currently visible.
        [[nodiscard]] bool is_visible() const;

        /// @brief Cycle time display format: UTC → LST → JD → UTC.       ← Task 3.7
        void toggle_time_format();

        /// @brief Get the current time display format.                    ← Task 3.7
        [[nodiscard]] TimeDisplayFormat get_time_format() const;

        /// @brief Access the internal BitmapFont for overlay label rendering.  ← SPRINT 04 Task 4.2
        [[nodiscard]] BitmapFont& get_font();

    private:
        /// @brief Draw the top-left panel (title + time).
        void draw_time_panel(f32 vw, f32 vh);

        /// @brief Draw the top-right panel (camera pointing + FOV).
        void draw_camera_panel(f32 vw, f32 vh);

        /// @brief Draw the bottom-left panel (observer location).
        void draw_observer_panel(f32 vw, f32 vh);

        /// @brief Draw the bottom-right panel (FPS + star count + time scale).
        void draw_performance_panel(f32 vw, f32 vh);

        /// @brief Draw the overlay status bar (bottom-center).            ← SPRINT 04 Task 4.7
        void draw_overlay_status(f32 vw, f32 vh);

        /// @brief Draw the sky-state line in the observer panel.          ← SPRINT 06 Task 6.7
        void draw_sky_state(f32 vw, f32 vh);

        BitmapFont m_font;
        HudData m_data{};
        bool m_visible = true;
        TimeDisplayFormat m_time_format = TimeDisplayFormat::kUtc;  ///< ← Task 3.7

        // -----------------------------------------------------------------
        // Layout constants
        // -----------------------------------------------------------------
        static constexpr f32 kMargin = 12.0f;       ///< Edge margin in pixels
        static constexpr f32 kScale  = 1.0f;        ///< Font scale (1× = 8×16)
        static constexpr f32 kGlyphW = 8.0f * kScale;
        static constexpr f32 kGlyphH = 16.0f * kScale;
        static constexpr f32 kLineSpacing = kGlyphH + 2.0f; ///< Pixels between lines

        // -----------------------------------------------------------------
        // Color scheme (retro terminal)
        // -----------------------------------------------------------------
        static constexpr Vec3f kColorLabel  = {0.0f, 0.667f, 0.0f};   ///< #00AA00
        static constexpr Vec3f kColorValue  = {0.0f, 1.0f,   0.0f};   ///< #00FF00
        static constexpr Vec3f kColorTitle  = {0.0f, 1.0f,   0.0f};   ///< #00FF00
        static constexpr Vec3f kColorDim    = {0.0f, 0.4f,   0.0f};   ///< #006600
    };

    // =====================================================================
    // Formatting helpers (free functions)
    // =====================================================================

    /// @brief Format Right Ascension as "HHh MMm SSs".
    /// @param ra_rad Right ascension in radians.
    /// @return Formatted string, e.g. "14h 23m 07s".
    [[nodiscard]] std::string format_ra(f64 ra_rad);

    /// @brief Format an angle as "±DD° MM' SS\"" (degrees, arcminutes, arcseconds).
    /// @param angle_rad Angle in radians.
    /// @return Formatted string, e.g. "+45 12' 33\"".
    [[nodiscard]] std::string format_dms(f64 angle_rad);

    /// @brief Format azimuth as "DDD° MM' SS\"".
    /// @param az_rad Azimuth in radians (0 = North, π/2 = East).
    /// @return Formatted string, e.g. "180 05' 21\"".
    [[nodiscard]] std::string format_az(f64 az_rad);

    /// @brief Format time scale for HUD display.
    /// @param scale Time scale multiplier (0 = paused, negative = reverse).
    /// @return Formatted string, e.g. "x1.0", "x1000", "PAUSED", "x-10".
    [[nodiscard]] std::string format_time_scale(f64 scale);

} // namespace parallax::ui