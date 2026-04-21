/// @file selection.cpp
/// @brief Object selection system implementation.
///
/// SPRINT 05 Task 5.5

#include "ui/selection.hpp"

#include "astro/coordinates.hpp"
#include "astro/solar_system.hpp"
#include "core/logger.hpp"
#include "rendering/solar_system_renderer.hpp"

#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

namespace parallax::ui
{

// =================================================================
// Star name loading
// =================================================================

bool Selection::load_star_names(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        PLX_CORE_WARN("Star names file not found: {}", path.string());
        return false;
    }

    std::string line;
    u32 count = 0;

    // Skip header line
    if (!std::getline(file, line))
    {
        return false;
    }

    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream iss(line);
        std::string hip_str;
        std::string common_name;
        std::string bayer;
        std::string constellation;
        std::string spectral;

        if (!std::getline(iss, hip_str, ','))      continue;
        if (!std::getline(iss, common_name, ','))   continue;
        if (!std::getline(iss, bayer, ','))          continue;
        if (!std::getline(iss, constellation, ','))  continue;
        std::getline(iss, spectral, ',');

        // Trim whitespace
        auto trim = [](std::string& s)
        {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ')  s.pop_back();
        };

        trim(hip_str);
        trim(common_name);
        trim(bayer);
        trim(constellation);
        trim(spectral);

        if (hip_str.empty())
        {
            continue;
        }

        const u32 hip_id = static_cast<u32>(std::stoul(hip_str));

        m_star_names[hip_id] = StarNameEntry{
            .hip_id = hip_id,
            .common_name = common_name,
            .bayer = bayer,
            .constellation = constellation,
            .spectral_type = spectral,
        };

        ++count;
    }

    PLX_CORE_INFO("Star names loaded: {} entries from {}", count, path.string());
    return count > 0;
}

// =================================================================
// Universe path — try_select_from_objects
// =================================================================

void Selection::try_select_from_objects(Vec2f click_ndc,
                                        std::span<const universe::CelestialObject> objects,
                                        const astro::ObserverLocation& observer,
                                        f64 lst_rad,
                                        const rendering::Camera& camera,
                                        VkExtent2D viewport)
{
    (void)viewport; // Picking is in NDC space

    const auto pointing = camera.get_pointing();
    const f64 fov_rad   = camera.get_fov_rad();

    f32 best_dist_sq = kPickRadiusNdc * kPickRadiusNdc;
    bool found = false;
    SelectedObject candidate;

    // Iterate in reverse so foreground objects (planets, DSOs) win over
    // background stars at the same screen position.
    for (auto it = objects.rbegin(); it != objects.rend(); ++it)
    {
        const auto& obj = *it;

        const auto hz = astro::Coordinates::equatorial_to_horizontal(
            {obj.ra, obj.dec}, observer, lst_rad);

        const auto screen_opt = astro::Coordinates::horizontal_to_screen(
            hz, pointing, fov_rad);

        if (!screen_opt.has_value())
        {
            continue;
        }

        const Vec2f screen = screen_opt.value();
        const f32 dx = screen.x - click_ndc.x;
        const f32 dy = screen.y - click_ndc.y;
        const f32 dist_sq = dx * dx + dy * dy;

        if (dist_sq >= best_dist_sq)
        {
            continue;
        }

        best_dist_sq = dist_sq;
        found = true;

        candidate = {};
        candidate.ra_rad  = obj.ra;
        candidate.dec_rad = obj.dec;
        candidate.mag_v   = obj.mag_v;
        candidate.color_bv = obj.color_bv;
        candidate.alt_rad = hz.alt;
        candidate.az_rad  = hz.az;
        candidate.above_horizon = hz.alt >= 0.0;
        candidate.screen_ndc = screen;
        candidate.celestial_obj = obj;

        switch (obj.type)
        {
            case universe::ObjectType::Star:
            case universe::ObjectType::ProceduralStar:
            {
                candidate.type = SelectedObjectType::Star;
                candidate.is_procedural = (obj.type == universe::ObjectType::ProceduralStar);

                if (!candidate.is_procedural)
                {
                    // Real catalog star: recover HIP ID and name data.
                    if (const auto* sd = std::get_if<universe::StarData>(&obj.data))
                    {
                        candidate.hip_id = sd->hip_id;
                        const auto* name_entry = find_star_name(sd->hip_id);
                        if (name_entry)
                        {
                            candidate.common_name  = name_entry->common_name;
                            candidate.bayer        = name_entry->bayer;
                            candidate.constellation = name_entry->constellation;
                            candidate.spectral_type = name_entry->spectral_type;
                        }
                    }
                }
                else
                {
                    // Procedural star: no HIP, no name, no Bayer, no spectral type.
                    // Build a stable, unique designation from the low 40 bits of source_id.
                    const u64 source_id = universe::decode_source_id(obj.id);
                    candidate.designation = std::format("PRC-{:010X}", source_id & 0xFFFFFFFFFFULL);
                }
                break;
            }

            case universe::ObjectType::DeepSkyObject:
            {
                candidate.type = SelectedObjectType::Dso;
                candidate.designation = std::format("M{}", universe::decode_source_id(obj.id));
                if (const auto* dd = std::get_if<universe::DsoData>(&obj.data))
                {
                    candidate.dso_type   = dd->dso_type;
                    candidate.size_arcmin = dd->size_arcmin;
                }
                break;
            }

            case universe::ObjectType::SolarSystemBody:
            {
                candidate.type = SelectedObjectType::SolarSystem;
                const u64 body_index = universe::decode_source_id(obj.id);

                if (body_index == 0)
                {
                    candidate.body_id   = rendering::SolarSystemRenderer::kBodyIdSun;
                    candidate.body_name = "Sun";
                }
                else if (body_index == 1)
                {
                    candidate.body_id   = rendering::SolarSystemRenderer::kBodyIdMoon;
                    candidate.body_name = "Moon";
                }
                else
                {
                    // Planet: body_index 2-8
                    static constexpr std::array<u32, 7> kPlanetIds = {
                        astro::planet_id::kMercury, astro::planet_id::kVenus,
                        astro::planet_id::kMars,    astro::planet_id::kJupiter,
                        astro::planet_id::kSaturn,  astro::planet_id::kUranus,
                        astro::planet_id::kNeptune,
                    };
                    if (body_index >= 2 && body_index <= 8)
                    {
                        const u32 pid = kPlanetIds[body_index - 2];
                        candidate.body_id   = 10u + pid;
                        candidate.body_name = std::string(rendering::SolarSystemRenderer::planet_name(pid));
                    }
                    else
                    {
                        candidate.body_id = static_cast<u32>(body_index);
                    }
                }
                if (const auto* sd = std::get_if<universe::SolarSystemData>(&obj.data))
                {
                    candidate.distance_au             = static_cast<f64>(sd->distance_au);
                    candidate.angular_diameter_arcsec = sd->apparent_diameter_arcsec;
                    candidate.phase_angle_deg         = sd->phase_angle;
                    candidate.illumination            = sd->illumination;
                }
                break;
            }

            default:
                break;
        }
    }

    if (found)
    {
        m_selection = candidate;

        if (candidate.type == SelectedObjectType::Star)
        {
            if (candidate.is_procedural)
            {
                PLX_CORE_INFO("Selected procedural star: {} mag {:.2f}",
                              candidate.designation, candidate.mag_v);
            }
            else if (!candidate.common_name.empty())
            {
                PLX_CORE_INFO("Selected star: {} (HIP {}) mag {:.2f}",
                              candidate.common_name, candidate.hip_id, candidate.mag_v);
            }
            else
            {
                PLX_CORE_INFO("Selected star: HIP {} mag {:.2f}",
                              candidate.hip_id, candidate.mag_v);
            }
        }
        else if (candidate.type == SelectedObjectType::Dso)
        {
            PLX_CORE_INFO("Selected DSO: {} mag {:.1f}",
                          candidate.designation, candidate.mag_v);
        }
        else if (candidate.type == SelectedObjectType::SolarSystem)
        {
            PLX_CORE_INFO("Selected Solar System body: {} (body_id {}) mag {:.2f}",
                          candidate.body_name, candidate.body_id, candidate.mag_v);
        }
    }
    else
    {
        clear();
    }
}

void Selection::update_from_objects(std::span<const universe::CelestialObject> objects,
                                    std::span<const rendering::SolarSystemScreenObject> ss_objects,
                                    const astro::ObserverLocation& observer,
                                    f64 lst_rad,
                                    const rendering::Camera& camera)
{
    (void)objects; // RA/Dec stored in m_selection from try_select_from_objects

    if (m_selection.type == SelectedObjectType::None)
    {
        return;
    }

    if (m_selection.type == SelectedObjectType::SolarSystem)
    {
        // Refresh from latest SS screen objects (they carry phase/illumination data)
        for (const auto& ss : ss_objects)
        {
            if (ss.body_id == m_selection.body_id)
            {
                m_selection.alt_rad = ss.alt_rad;
                m_selection.az_rad  = ss.az_rad;
                m_selection.above_horizon = ss.alt_rad >= 0.0;
                m_selection.screen_ndc    = ss.screen_ndc;
                m_selection.distance_au             = ss.distance_au;
                m_selection.angular_diameter_arcsec = ss.angular_diameter_arcsec;
                m_selection.phase_angle_deg         = ss.phase_angle_deg;
                m_selection.illumination            = ss.illumination;
                m_selection.ra_rad  = ss.equatorial.ra;
                m_selection.dec_rad = ss.equatorial.dec;
                return;
            }
        }
        // Body not in current FOV — keep last position
        return;
    }

    // Stars and DSOs: re-project stored RA/Dec.
    const auto pointing = camera.get_pointing();
    const f64 fov_rad   = camera.get_fov_rad();

    const auto hz = astro::Coordinates::equatorial_to_horizontal(
        {m_selection.ra_rad, m_selection.dec_rad}, observer, lst_rad);

    m_selection.alt_rad       = hz.alt;
    m_selection.az_rad        = hz.az;
    m_selection.above_horizon = hz.alt >= 0.0;

    const auto screen_opt = astro::Coordinates::horizontal_to_screen(hz, pointing, fov_rad);
    if (screen_opt.has_value())
    {
        m_selection.screen_ndc = screen_opt.value();
    }
}

// =================================================================
// Selection attempt (legacy)
// =================================================================

void Selection::try_select(Vec2f click_ndc,
                           std::span<const catalog::StarEntry> stars,
                           std::span<const u32> visible_star_indices,
                           std::span<const Vec2f> star_screen_positions,
                           std::span<const catalog::DsoEntry> dsos,
                           const astro::ObserverLocation& observer,
                           f64 lst_rad,
                           const rendering::Camera& camera,
                           VkExtent2D viewport)
{
    // Delegate to the Solar System-aware overload with an empty SS span.
    try_select(click_ndc,
               stars, visible_star_indices, star_screen_positions,
               dsos,
               std::span<const rendering::SolarSystemScreenObject>{},
               observer, lst_rad, camera, viewport);
}

void Selection::try_select(Vec2f click_ndc,
                           std::span<const catalog::StarEntry> stars,
                           std::span<const u32> visible_star_indices,
                           std::span<const Vec2f> star_screen_positions,
                           std::span<const catalog::DsoEntry> dsos,
                           std::span<const rendering::SolarSystemScreenObject> ss_objects,
                           const astro::ObserverLocation& observer,
                           f64 lst_rad,
                           const rendering::Camera& camera,
                           VkExtent2D viewport)
{
    (void)viewport;  // Not needed — picking is in NDC space

    f32 best_dist_sq = kPickRadiusNdc * kPickRadiusNdc;
    bool found = false;

    SelectedObject candidate;

    // -----------------------------------------------------------------
    // Search Solar System bodies first (priority: SS > Star > DSO)
    // -----------------------------------------------------------------
    for (const auto& ss : ss_objects)
    {
        const f32 dx = ss.screen_ndc.x - click_ndc.x;
        const f32 dy = ss.screen_ndc.y - click_ndc.y;
        const f32 dist_sq = dx * dx + dy * dy;
        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            found = true;
            candidate.type = SelectedObjectType::SolarSystem;
            candidate.body_id = ss.body_id;
            candidate.body_name = std::string(ss.name);
            candidate.ra_rad = ss.equatorial.ra;
            candidate.dec_rad = ss.equatorial.dec;
            candidate.mag_v = ss.magnitude;
            candidate.distance_au = ss.distance_au;
            candidate.angular_diameter_arcsec = ss.angular_diameter_arcsec;
            candidate.phase_angle_deg = ss.phase_angle_deg;
            candidate.illumination = ss.illumination;
            candidate.alt_rad = ss.alt_rad;
            candidate.az_rad = ss.az_rad;
            candidate.above_horizon = ss.alt_rad >= 0.0;
            candidate.screen_ndc = ss.screen_ndc;
            // Clear star/DSO-specific fields so stale data doesn't leak.
            candidate.hip_id = 0;
            candidate.common_name.clear();
            candidate.bayer.clear();
            candidate.constellation.clear();
            candidate.spectral_type.clear();
            candidate.designation.clear();
            candidate.dso_common_name.clear();
        }
    }

    // -----------------------------------------------------------------
    // Search visible stars
    // -----------------------------------------------------------------
    for (std::size_t i = 0; i < visible_star_indices.size() && i < star_screen_positions.size(); ++i)
    {
        const Vec2f screen = star_screen_positions[i];
        const f32 dx = screen.x - click_ndc.x;
        const f32 dy = screen.y - click_ndc.y;
        const f32 dist_sq = dx * dx + dy * dy;

        if (dist_sq < best_dist_sq)
        {
            const u32 star_idx = visible_star_indices[i];
            if (star_idx >= stars.size()) continue;

            const auto& star = stars[star_idx];

            best_dist_sq = dist_sq;
            found = true;

            candidate.type = SelectedObjectType::Star;
            candidate.star_index = star_idx;
            candidate.hip_id = star.catalog_id;
            candidate.ra_rad = star.ra;
            candidate.dec_rad = star.dec;
            candidate.mag_v = star.mag_v;
            candidate.color_bv = star.color_bv;
            candidate.screen_ndc = screen;

            // Look up name
            const auto* name_entry = find_star_name(star.catalog_id);
            if (name_entry)
            {
                candidate.common_name = name_entry->common_name;
                candidate.bayer = name_entry->bayer;
                candidate.constellation = name_entry->constellation;
                candidate.spectral_type = name_entry->spectral_type;
            }
            else
            {
                candidate.common_name.clear();
                candidate.bayer.clear();
                candidate.constellation.clear();
                candidate.spectral_type.clear();
            }
        }
    }

    // -----------------------------------------------------------------
    // Search DSOs (project each visible DSO to screen)
    // -----------------------------------------------------------------
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();

    for (std::size_t i = 0; i < dsos.size(); ++i)
    {
        const auto& dso = dsos[i];

        // Magnitude check
        if (dso.mag_v > camera.get_magnitude_limit())
        {
            continue;
        }

        // Project to screen
        auto screen_opt = astro::Coordinates::project_radec_to_screen(
            dso.ra, dso.dec, observer, lst_rad, pointing, fov_rad);

        if (!screen_opt.has_value())
        {
            continue;
        }

        const Vec2f screen = screen_opt.value();
        const f32 dx = screen.x - click_ndc.x;
        const f32 dy = screen.y - click_ndc.y;
        const f32 dist_sq = dx * dx + dy * dy;

        if (dist_sq < best_dist_sq)
        {
            best_dist_sq = dist_sq;
            found = true;

            candidate.type = SelectedObjectType::Dso;
            candidate.dso_index = static_cast<u32>(i);
            candidate.ra_rad = dso.ra;
            candidate.dec_rad = dso.dec;
            candidate.mag_v = dso.mag_v;
            candidate.designation = dso.designation;
            candidate.dso_common_name = dso.common_name;
            candidate.dso_type = dso.type;
            candidate.size_arcmin = dso.size_arcmin;
            candidate.screen_ndc = screen;

            // Clear star-specific fields
            candidate.hip_id = 0;
            candidate.color_bv = 0.0f;
            candidate.common_name.clear();
            candidate.bayer.clear();
            candidate.spectral_type.clear();
        }
    }

    if (found)
    {
        // For Solar System bodies, Alt/Az was already set from the screen object
        // (which is updated each frame by SolarSystemRenderer). For stars and DSOs,
        // compute Alt/Az from RA/Dec now.
        if (candidate.type != SelectedObjectType::SolarSystem)
        {
            const auto hz = astro::Coordinates::equatorial_to_horizontal(
                {candidate.ra_rad, candidate.dec_rad}, observer, lst_rad);
            candidate.alt_rad = hz.alt;
            candidate.az_rad = hz.az;
            candidate.above_horizon = hz.alt >= 0.0;
        }

        m_selection = candidate;

        if (candidate.type == SelectedObjectType::Star)
        {
            if (!candidate.common_name.empty())
            {
                PLX_CORE_INFO("Selected star: {} (HIP {}) mag {:.2f}",
                              candidate.common_name, candidate.hip_id, candidate.mag_v);
            }
            else
            {
                PLX_CORE_INFO("Selected star: HIP {} mag {:.2f}",
                              candidate.hip_id, candidate.mag_v);
            }
        }
        else if (candidate.type == SelectedObjectType::Dso)
        {
            PLX_CORE_INFO("Selected DSO: {} ({}) mag {:.1f}",
                          candidate.designation, candidate.dso_common_name, candidate.mag_v);
        }
        else
        {
            PLX_CORE_INFO("Selected Solar System body: {} (body_id {}) mag {:.2f}",
                          candidate.body_name, candidate.body_id, candidate.mag_v);
        }
    }
    else
    {
        clear();
    }
}

// =================================================================
// Per-frame update
// =================================================================

void Selection::update(std::span<const catalog::StarEntry> stars,
                       std::span<const catalog::DsoEntry> dsos,
                       const astro::ObserverLocation& observer,
                       f64 lst_rad,
                       const rendering::Camera& camera)
{
    // Delegate to the Solar System-aware overload with an empty SS span.
    update(stars, dsos,
           std::span<const rendering::SolarSystemScreenObject>{},
           observer, lst_rad, camera);
}

void Selection::update(std::span<const catalog::StarEntry> stars,
                       std::span<const catalog::DsoEntry> dsos,
                       std::span<const rendering::SolarSystemScreenObject> ss_objects,
                       const astro::ObserverLocation& observer,
                       f64 lst_rad,
                       const rendering::Camera& camera)
{
    // Stars/DSOs spans are available for future use (e.g. re-reading updated data)
    // but RA/Dec are already stored in m_selection from try_select().
    (void)stars;
    (void)dsos;

    if (m_selection.type == SelectedObjectType::None)
    {
        return;
    }

    // -----------------------------------------------------------------
    // Solar System: refresh from latest screen objects
    // Tracking a Solar System body (e.g. Moon) works the same as stars:
    // the RA/Dec stored here is updated each frame, so the camera tracks
    // the body as it moves across the sky.
    // -----------------------------------------------------------------
    if (m_selection.type == SelectedObjectType::SolarSystem)
    {
        for (const auto& ss : ss_objects)
        {
            if (ss.body_id == m_selection.body_id)
            {
                m_selection.ra_rad = ss.equatorial.ra;
                m_selection.dec_rad = ss.equatorial.dec;
                m_selection.alt_rad = ss.alt_rad;
                m_selection.az_rad = ss.az_rad;
                m_selection.above_horizon = ss.alt_rad >= 0.0;
                m_selection.screen_ndc = ss.screen_ndc;
                m_selection.mag_v = ss.magnitude;
                m_selection.distance_au = ss.distance_au;
                m_selection.angular_diameter_arcsec = ss.angular_diameter_arcsec;
                m_selection.phase_angle_deg = ss.phase_angle_deg;
                m_selection.illumination = ss.illumination;
                return;
            }
        }
        // Body not in current list (shouldn't happen, but defensive — keep stale position)
        PLX_CORE_WARN("Selection::update: Solar System body_id {} not found in current screen objects",
                      m_selection.body_id);
        return;
    }

    // Re-project RA/Dec to screen each frame (object position changes with time)
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();

    // Recompute Alt/Az
    const auto hz = astro::Coordinates::equatorial_to_horizontal(
        {m_selection.ra_rad, m_selection.dec_rad}, observer, lst_rad);
    m_selection.alt_rad = hz.alt;
    m_selection.az_rad = hz.az;
    m_selection.above_horizon = hz.alt >= 0.0;

    // Recompute screen position
    auto screen_opt = astro::Coordinates::project_radec_to_screen(
        m_selection.ra_rad, m_selection.dec_rad,
        observer, lst_rad, pointing, fov_rad);

    if (screen_opt.has_value())
    {
        m_selection.screen_ndc = screen_opt.value();
    }
    else
    {
        // Object is off-screen or below horizon — keep selection but don't render indicator
        m_selection.screen_ndc = {999.0f, 999.0f};
    }
}

// =================================================================
// Render selection indicator
// =================================================================

void Selection::render_indicator(rendering::LineRenderer& lines,
                                 VkExtent2D viewport) const
{
    (void)viewport;  // Indicator is drawn in NDC space

    if (m_selection.type == SelectedObjectType::None)
    {
        return;
    }

    const Vec2f c = m_selection.screen_ndc;

    // Don't draw if off-screen
    if (std::abs(c.x) > 1.2f || std::abs(c.y) > 1.2f)
    {
        return;
    }

    // Circle
    lines.add_circle(c, kIndicatorRadiusNdc, kIndicatorColor, 32);

    // Crosshair arms (with gap around the circle)
    const f32 gap = kCrosshairGapNdc;
    const f32 arm = kCrosshairArmNdc;

    // Right arm
    lines.add_line({c.x + gap, c.y}, {c.x + gap + arm, c.y}, kIndicatorColor);
    // Left arm
    lines.add_line({c.x - gap, c.y}, {c.x - gap - arm, c.y}, kIndicatorColor);
    // Top arm
    lines.add_line({c.x, c.y - gap}, {c.x, c.y - gap - arm}, kIndicatorColor);
    // Bottom arm
    lines.add_line({c.x, c.y + gap}, {c.x, c.y + gap + arm}, kIndicatorColor);
}

// =================================================================
// Clear / Queries
// =================================================================

void Selection::clear()
{
    if (m_selection.type != SelectedObjectType::None)
    {
        PLX_CORE_TRACE("Selection cleared");
    }
    m_selection = SelectedObject{};
    m_tracking = false;
}

bool Selection::has_selection() const
{
    return m_selection.type != SelectedObjectType::None;
}

const SelectedObject& Selection::get_selection() const
{
    return m_selection;
}

void Selection::set_tracking(bool enabled)
{
    m_tracking = enabled && m_selection.type != SelectedObjectType::None;
    if (m_tracking)
    {
        PLX_CORE_INFO("Tracking enabled for selected object");
    }
    else
    {
        PLX_CORE_TRACE("Tracking disabled");
    }
}

bool Selection::is_tracking() const
{
    return m_tracking && m_selection.type != SelectedObjectType::None;
}

std::optional<astro::EquatorialCoord> Selection::get_track_target() const
{
    if (!is_tracking())
    {
        return std::nullopt;
    }

    return astro::EquatorialCoord{m_selection.ra_rad, m_selection.dec_rad};
}

const StarNameEntry* Selection::find_star_name(u32 hip_id) const
{
    const auto it = m_star_names.find(hip_id);
    if (it != m_star_names.end())
    {
        return &it->second;
    }
    return nullptr;
}

u32 Selection::get_name_count() const
{
    return static_cast<u32>(m_star_names.size());
}

} // namespace parallax::ui