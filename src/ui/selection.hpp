#pragma once

/// @file selection.hpp
/// @brief Object selection system — click sky to pick nearest star/DSO.
///
/// Supports:
///   - Click picking with configurable pixel radius.
///   - Selection indicator rendering (yellow crosshair + circle).
///   - Track mode: camera follows selected object as time advances.
///   - Named star lookup via star_names.csv (HIP → common name + Bayer).
///
/// SPRINT 05 Task 5.5

#include "astro/coordinates.hpp"
#include "catalog/dso_entry.hpp"
#include "catalog/star_entry.hpp"
#include "core/types.hpp"
#include "rendering/camera.hpp"
#include "rendering/line_renderer.hpp"
#include "rendering/solar_system_renderer.hpp"
#include "ui/font.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace parallax::ui
{

// =================================================================
// SelectedObjectType
// =================================================================

/// @brief What type of object is currently selected.
enum class SelectedObjectType : u8
{
    None,
    Star,
    Dso,
    SolarSystem
};

// =================================================================
// StarNameEntry — one named star from star_names.csv
// =================================================================

/// @brief Common/Bayer name for a star, keyed by HIP ID.
struct StarNameEntry
{
    u32 hip_id;
    std::string common_name;    ///< e.g. "Sirius", "Betelgeuse"
    std::string bayer;          ///< e.g. "Alpha CMa", "Alpha Ori"
    std::string constellation;  ///< 3-letter IAU code, e.g. "CMa"
    std::string spectral_type;  ///< e.g. "A1V", "M2Iab"
};

// =================================================================
// SelectedObject — full information about the current selection
// =================================================================

/// @brief All information about the currently selected object, used by InfoPanel.
struct SelectedObject
{
    SelectedObjectType type = SelectedObjectType::None;

    // Star fields (valid when type == Star)
    u32 star_index = 0;             ///< Index into m_stars vector
    u32 hip_id = 0;                 ///< Hipparcos catalog ID
    f64 ra_rad = 0.0;              ///< J2000 RA (radians)
    f64 dec_rad = 0.0;             ///< J2000 Dec (radians)
    f32 mag_v = 0.0f;              ///< Visual magnitude
    f32 color_bv = 0.0f;           ///< B-V color index
    std::string common_name;        ///< e.g. "Sirius" (empty if unnamed)
    std::string bayer;              ///< e.g. "Alpha CMa"
    std::string constellation;      ///< 3-letter IAU abbreviation
    std::string spectral_type;      ///< Spectral class

    // DSO fields (valid when type == Dso)
    u32 dso_index = 0;              ///< Index into m_dsos vector
    std::string designation;        ///< e.g. "M42"
    std::string dso_common_name;    ///< e.g. "Orion Nebula"
    catalog::DsoType dso_type = catalog::DsoType::Other;
    f32 size_arcmin = 0.0f;

    // SolarSystem fields (valid when type == SolarSystem)
    u32 body_id = 0;                         ///< 0=Sun, 1=Moon, 10+planet_id (see SolarSystemRenderer)
    std::string body_name;                   ///< "Sun", "Moon", "Jupiter", ...
    f64 distance_au = 0.0;                   ///< Distance from Earth (AU)
    f32 angular_diameter_arcsec = 0.0f;
    f32 phase_angle_deg = 0.0f;
    f32 illumination = 0.0f;                 ///< 0..1

    // Computed each frame (valid for both Star and Dso)
    f64 alt_rad = 0.0;
    f64 az_rad = 0.0;
    Vec2f screen_ndc = {0.0f, 0.0f};   ///< Screen NDC of selected object

    /// @brief True if the object is above the horizon.
    bool above_horizon = false;
};

// =================================================================
// Selection
// =================================================================

/// @brief Object selection system: click-pick, tracking, name lookup.
///
/// Lifecycle:
///   1. load_star_names()   — parse data/catalogs/star_names.csv
///   2. try_select()        — called on left-click, finds nearest object within radius
///   3. update()            — called each frame to refresh screen position + Alt/Az
///   4. render_indicator()  — draws yellow crosshair/circle around selected object
///   5. clear()             — deselect
class Selection
{
public:
    Selection() = default;
    ~Selection() = default;

    Selection(const Selection&) = delete;
    Selection& operator=(const Selection&) = delete;
    Selection(Selection&&) = default;
    Selection& operator=(Selection&&) = default;

    /// @brief Load star names from CSV file.
    /// @param path Path to data/catalogs/star_names.csv.
    /// @return True if at least some names were loaded.
    [[nodiscard]] bool load_star_names(const std::filesystem::path& path);

    /// @brief Attempt to select the nearest star or DSO to the click position.
    ///
    /// Searches stars first, then DSOs. Takes the nearest object within the
    /// pixel radius. If nothing is found, clears the current selection.
    ///
    /// @param click_ndc   Click position in NDC [-1, 1].
    /// @param stars       The loaded star catalog.
    /// @param visible_star_indices Indices of stars currently on screen.
    /// @param star_screen_positions Screen NDC for each visible star (parallel to visible_star_indices).
    /// @param dsos        The loaded DSO catalog.
    /// @param observer    Observer location.
    /// @param lst_rad     Local sidereal time (radians).
    /// @param camera      Current camera state.
    /// @param viewport    Current viewport dimensions.
    void try_select(Vec2f click_ndc,
                    std::span<const catalog::StarEntry> stars,
                    std::span<const u32> visible_star_indices,
                    std::span<const Vec2f> star_screen_positions,
                    std::span<const catalog::DsoEntry> dsos,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    const rendering::Camera& camera,
                    VkExtent2D viewport);

    /// @brief Attempt to select nearest Star, DSO, or Solar System body.
    ///
    /// Priority within kPickRadiusNdc: closest pixel distance wins regardless of type.
    /// If two objects are equidistant, Solar System > Star > DSO (Solar System bodies
    /// are rare and almost always the intended target when clicked).
    void try_select(Vec2f click_ndc,
                    std::span<const catalog::StarEntry> stars,
                    std::span<const u32> visible_star_indices,
                    std::span<const Vec2f> star_screen_positions,
                    std::span<const catalog::DsoEntry> dsos,
                    std::span<const rendering::SolarSystemScreenObject> ss_objects,
                    const astro::ObserverLocation& observer,
                    f64 lst_rad,
                    const rendering::Camera& camera,
                    VkExtent2D viewport);

    /// @brief Update the selected object's screen position and Alt/Az for this frame.
    ///
    /// Must be called each frame after simulation update, before rendering.
    void update(std::span<const catalog::StarEntry> stars,
                std::span<const catalog::DsoEntry> dsos,
                const astro::ObserverLocation& observer,
                f64 lst_rad,
                const rendering::Camera& camera);

    /// @brief Update the selected object's screen position and Alt/Az for this frame.
    ///
    /// Overload that also refreshes Solar System body positions from the latest
    /// rendered screen objects. Must be called after SolarSystemRenderer::update().
    void update(std::span<const catalog::StarEntry> stars,
                std::span<const catalog::DsoEntry> dsos,
                std::span<const rendering::SolarSystemScreenObject> ss_objects,
                const astro::ObserverLocation& observer,
                f64 lst_rad,
                const rendering::Camera& camera);

    /// @brief Render the selection indicator: yellow crosshair + circle.
    void render_indicator(rendering::LineRenderer& lines,
                          VkExtent2D viewport) const;

    /// @brief Clear the current selection.
    void clear();

    /// @brief True if an object is currently selected.
    [[nodiscard]] bool has_selection() const;

    /// @brief Get the current selection (read-only).
    [[nodiscard]] const SelectedObject& get_selection() const;

    /// @brief Enable tracking mode: camera follows selected object.
    void set_tracking(bool enabled);

    /// @brief True if tracking mode is active.
    [[nodiscard]] bool is_tracking() const;

    /// @brief If tracking, returns the RA/Dec the camera should point at.
    [[nodiscard]] std::optional<astro::EquatorialCoord> get_track_target() const;

    /// @brief Look up a star name by HIP ID.
    [[nodiscard]] const StarNameEntry* find_star_name(u32 hip_id) const;

    /// @brief Number of star names loaded.
    [[nodiscard]] u32 get_name_count() const;

private:
    SelectedObject m_selection;
    bool m_tracking = false;

    /// @brief HIP ID → star name data.
    std::unordered_map<u32, StarNameEntry> m_star_names;

    /// @brief Pick radius in NDC units (���15px at 1080p).
    static constexpr f32 kPickRadiusNdc = 0.028f;

    /// @brief Selection indicator color: bright yellow.
    static constexpr Vec4f kIndicatorColor{1.0f, 1.0f, 0.0f, 0.9f};

    /// @brief Selection indicator circle radius in NDC.
    static constexpr f32 kIndicatorRadiusNdc = 0.025f;

    /// @brief Crosshair arm length in NDC.
    static constexpr f32 kCrosshairArmNdc = 0.04f;

    /// @brief Gap between circle and crosshair arm.
    static constexpr f32 kCrosshairGapNdc = 0.03f;
};

} // namespace parallax::ui