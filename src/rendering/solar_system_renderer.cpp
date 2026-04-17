/// @file solar_system_renderer.cpp
/// @brief Skychart Solar System renderer — Sun, Moon, and planet icons.
///
/// SKYCHART MODE: schematic rendering, not physically realistic.
/// Uses the same project_radec_to_screen pipeline as stars/constellations.
/// No magnitude filtering — Solar System bodies are always rendered.
///
/// SPRINT 06 Task 6.5

#include "rendering/solar_system_renderer.hpp"

#include "astro/coordinates.hpp"
#include "core/logger.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace parallax::rendering
{

// =================================================================
// Planet identifier array — matches kPlanetIds in compute_all()
//   planets[0] = Mercury (id=1), [1] = Venus (id=2), [2] = Mars (id=4)
//   [3] = Jupiter (id=5), [4] = Saturn (id=6), [5] = Uranus (id=7),
//   [6] = Neptune (id=8)
// =================================================================
static constexpr std::array<u32, 7> kPlanetIdMap = {
    astro::planet_id::kMercury,
    astro::planet_id::kVenus,
    astro::planet_id::kMars,
    astro::planet_id::kJupiter,
    astro::planet_id::kSaturn,
    astro::planet_id::kUranus,
    astro::planet_id::kNeptune,
};

// =================================================================
// Public API
// =================================================================

void SolarSystemRenderer::update(const astro::SolarSystem::AllBodies& bodies,
                                 const astro::MoonState& moon_state,
                                 const Camera& camera,
                                 const astro::ObserverLocation& observer,
                                 f64 lst_rad,
                                 LineRenderer& lines,
                                 ui::BitmapFont& font,
                                 VkExtent2D viewport,
                                 bool atmosphere_on)
{
    m_rendered_count = 0;
    m_screen_objects.clear();

    if (!m_visible)
    {
        return;
    }

    render_sun(bodies.sun, camera, observer, lst_rad, lines, font, viewport, atmosphere_on);
    render_moon(bodies.moon, moon_state, camera, observer, lst_rad, lines, font, viewport, atmosphere_on);

    for (u32 i = 0; i < 7; ++i)
    {
        render_planet(bodies.planets[i], kPlanetIdMap[i],
                      camera, observer, lst_rad,
                      lines, font, viewport, atmosphere_on);
    }
}

void SolarSystemRenderer::set_visible(bool visible)
{
    m_visible = visible;
}

void SolarSystemRenderer::toggle_visible()
{
    m_visible = !m_visible;
    PLX_CORE_INFO("Solar System overlay: {}", m_visible ? "ON" : "OFF");
}

bool SolarSystemRenderer::is_visible() const
{
    return m_visible;
}

u32 SolarSystemRenderer::get_rendered_count() const
{
    return m_rendered_count;
}

std::span<const SolarSystemScreenObject> SolarSystemRenderer::get_screen_objects() const
{
    return std::span<const SolarSystemScreenObject>(m_screen_objects);
}

// =================================================================
// Sun rendering
// =================================================================

void SolarSystemRenderer::render_sun(const astro::CelestialBodyState& sun,
                                     const Camera& camera,
                                     const astro::ObserverLocation& observer,
                                     f64 lst_rad,
                                     LineRenderer& lines,
                                     ui::BitmapFont& font,
                                     VkExtent2D viewport,
                                     bool atmosphere_on)
{
    const auto proj = project_body(sun, camera, observer, lst_rad, atmosphere_on);
    if (!proj)
    {
        return;
    }

    const Vec2f pos = proj->screen_ndc;

    // Draw filled circle approximation (3 concentric circles)
    draw_filled_circle(pos, kSunRadiusNdc, kSunColor, lines);

    // Four subtle rays at cardinal directions
    constexpr f32 kRayInner = 1.1f;
    constexpr f32 kRayOuter = 1.6f;
    const f32 ray_in  = kSunRadiusNdc * kRayInner;
    const f32 ray_out = kSunRadiusNdc * kRayOuter;
    for (u32 d = 0; d < 4; ++d)
    {
        const f32 angle = static_cast<f32>(d) * glm::half_pi<f32>();
        const f32 cos_a = std::cos(angle);
        const f32 sin_a = std::sin(angle);
        lines.add_line(Vec2f{pos.x + cos_a * ray_in,  pos.y + sin_a * ray_in},
                       Vec2f{pos.x + cos_a * ray_out, pos.y + sin_a * ray_out},
                       kSunColor);
    }

    // Label "Sun"
    const Vec2f px = ndc_to_pixel(pos, viewport);
    const f32 offset_x = kLabelOffsetNdc.x * static_cast<f32>(viewport.width) * 0.5f;
    const f32 offset_y = kLabelOffsetNdc.y * static_cast<f32>(viewport.height) * 0.5f;
    font.draw_text("Sun",
                   px.x + offset_x,
                   px.y - offset_y,
                   kLabelScale,
                   kSunLabelColor);

    m_screen_objects.push_back(SolarSystemScreenObject{
        .screen_ndc            = pos,
        .equatorial            = sun.equatorial,
        .alt_rad               = proj->alt_rad,
        .az_rad                = proj->az_rad,
        .distance_au           = sun.distance_au,
        .magnitude             = sun.magnitude,
        .angular_diameter_arcsec = sun.angular_diameter_arcsec,
        .phase_angle_deg       = sun.phase_angle_deg,
        .illumination          = sun.illumination,
        .body_id               = kBodyIdSun,
        .name                  = "Sun",
    });
    ++m_rendered_count;
}

// =================================================================
// Moon rendering
// =================================================================

void SolarSystemRenderer::render_moon(const astro::CelestialBodyState& moon,
                                      const astro::MoonState& moon_state,
                                      const Camera& camera,
                                      const astro::ObserverLocation& observer,
                                      f64 lst_rad,
                                      LineRenderer& lines,
                                      ui::BitmapFont& font,
                                      VkExtent2D viewport,
                                      bool atmosphere_on)
{
    const auto proj = project_body(moon, camera, observer, lst_rad, atmosphere_on);
    if (!proj)
    {
        return;
    }

    const Vec2f pos = proj->screen_ndc;

    // Draw filled circle
    draw_filled_circle(pos, kMoonRadiusNdc, kMoonColor, lines);

    // Phase shadow — waxing when elongation < 180°
    const bool waxing = (moon_state.elongation_deg < 180.0);
    draw_moon_phase(pos, kMoonRadiusNdc, moon_state.body.illumination, waxing, lines);

    // Label "Moon"
    const Vec2f px = ndc_to_pixel(pos, viewport);
    const f32 offset_x = kLabelOffsetNdc.x * static_cast<f32>(viewport.width) * 0.5f;
    const f32 offset_y = kLabelOffsetNdc.y * static_cast<f32>(viewport.height) * 0.5f;
    font.draw_text("Moon",
                   px.x + offset_x,
                   px.y - offset_y,
                   kLabelScale,
                   kMoonLabelColor);

    m_screen_objects.push_back(SolarSystemScreenObject{
        .screen_ndc            = pos,
        .equatorial            = moon.equatorial,
        .alt_rad               = proj->alt_rad,
        .az_rad                = proj->az_rad,
        .distance_au           = moon.distance_au,
        .magnitude             = moon.magnitude,
        .angular_diameter_arcsec = moon.angular_diameter_arcsec,
        .phase_angle_deg       = moon.phase_angle_deg,
        .illumination          = moon.illumination,
        .body_id               = kBodyIdMoon,
        .name                  = "Moon",
    });
    ++m_rendered_count;
}

// =================================================================
// Planet rendering
// =================================================================

void SolarSystemRenderer::render_planet(const astro::CelestialBodyState& body,
                                        u32 pid,
                                        const Camera& camera,
                                        const astro::ObserverLocation& observer,
                                        f64 lst_rad,
                                        LineRenderer& lines,
                                        ui::BitmapFont& font,
                                        VkExtent2D viewport,
                                        bool atmosphere_on)
{
    const auto proj = project_body(body, camera, observer, lst_rad, atmosphere_on);
    if (!proj)
    {
        return;
    }

    const Vec2f pos  = proj->screen_ndc;
    const Vec4f col  = planet_color(pid);
    const f32 radius = magnitude_to_radius_ndc(body.magnitude);

    draw_filled_circle(pos, radius, col, lines);

    // Saturn ring hint: horizontal line at ±1.8× radius
    if (pid == astro::planet_id::kSaturn)
    {
        lines.add_line(Vec2f{pos.x - radius * 1.8f, pos.y},
                       Vec2f{pos.x + radius * 1.8f, pos.y},
                       col);
    }

    // Label
    const Vec2f px = ndc_to_pixel(pos, viewport);
    const f32 offset_x = kLabelOffsetNdc.x * static_cast<f32>(viewport.width) * 0.5f;
    const f32 offset_y = kLabelOffsetNdc.y * static_cast<f32>(viewport.height) * 0.5f;
    font.draw_text(std::string(planet_name(pid)),
                   px.x + offset_x,
                   px.y - offset_y,
                   kLabelScale,
                   kPlanetLabelColor);

    m_screen_objects.push_back(SolarSystemScreenObject{
        .screen_ndc            = pos,
        .equatorial            = body.equatorial,
        .alt_rad               = proj->alt_rad,
        .az_rad                = proj->az_rad,
        .distance_au           = body.distance_au,
        .magnitude             = body.magnitude,
        .angular_diameter_arcsec = body.angular_diameter_arcsec,
        .phase_angle_deg       = body.phase_angle_deg,
        .illumination          = body.illumination,
        .body_id               = 10u + pid,
        .name                  = planet_name(pid),
    });
    ++m_rendered_count;
}

// =================================================================
// Projection helper
// =================================================================

std::optional<SolarSystemRenderer::ProjectionResult>
SolarSystemRenderer::project_body(const astro::CelestialBodyState& body,
                                  const Camera& camera,
                                  const astro::ObserverLocation& observer,
                                  f64 lst_rad,
                                  bool atmosphere_on)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad   = camera.get_fov_rad();

    const auto hz = astro::Coordinates::equatorial_to_horizontal(
        body.equatorial, observer, lst_rad);

    std::optional<Vec2f> screen;
    if (atmosphere_on)
    {
        // Shared pipeline — horizon cull + FOV cull + gnomonic projection
        screen = astro::Coordinates::project_radec_to_screen(
            body.equatorial.ra, body.equatorial.dec,
            observer, lst_rad, pointing, fov_rad);
    }
    else
    {
        // Atmosphere off → bypass horizon cull, keep FOV cull
        screen = astro::Coordinates::horizontal_to_screen(hz, pointing, fov_rad);
    }

    if (!screen)
    {
        return std::nullopt;
    }

    return ProjectionResult{
        .screen_ndc = *screen,
        .alt_rad    = hz.alt,
        .az_rad     = hz.az,
    };
}

// =================================================================
// Draw helpers
// =================================================================

void SolarSystemRenderer::draw_filled_circle(Vec2f center_ndc,
                                              f32 radius_ndc,
                                              Vec4f color,
                                              LineRenderer& lines,
                                              u32 segments)
{
    // Approximate a filled disc with 3 concentric line-circles
    // at 100%, 66%, and 33% of the radius. At 6-8 px this looks solid.
    const f32 step = glm::two_pi<f32>() / static_cast<f32>(segments);

    for (u32 ring = 0; ring < 3; ++ring)
    {
        const f32 r = radius_ndc * (1.0f - static_cast<f32>(ring) * 0.33f);
        Vec2f prev{};
        for (u32 i = 0; i <= segments; ++i)
        {
            const f32 angle = static_cast<f32>(i) * step;
            const Vec2f cur{center_ndc.x + r * std::cos(angle),
                            center_ndc.y + r * std::sin(angle)};
            if (i > 0)
            {
                lines.add_line(prev, cur, color);
            }
            prev = cur;
        }
    }
}

void SolarSystemRenderer::draw_moon_phase(Vec2f center_ndc,
                                          f32 radius_ndc,
                                          f32 illumination,
                                          bool waxing,
                                          LineRenderer& lines)
{
    // Full moon — no shadow overlay needed
    if (illumination > 0.98f)
    {
        return;
    }

    // New moon — entire disc shadowed
    if (illumination < 0.02f)
    {
        draw_filled_circle(center_ndc, radius_ndc, kMoonShadowColor, lines);
        return;
    }

    // Horizontal line segments across the shadowed hemisphere.
    // terminator_x = (1 - 2*illumination) * radius (simplified linear model):
    //   illumination=0.25 → terminator at +0.5r (right of center, waxing crescent shadow)
    //   illumination=0.50 → terminator at 0 (center, first quarter)
    //   illumination=0.75 → terminator at -0.5r (left of center, waxing gibbous shadow is small)
    const f32 t_offset = (1.0f - 2.0f * illumination) * radius_ndc;

    // Draw short horizontal line segments across the shadowed hemisphere
    // to provide a visual darkening hint.  We sweep from disc edge to terminator.
    const u32 kLines = 12;
    for (u32 j = 0; j <= kLines; ++j)
    {
        const f32 frac = static_cast<f32>(j) / static_cast<f32>(kLines);
        const f32 y    = center_ndc.y + radius_ndc * (2.0f * frac - 1.0f);
        const f32 dy   = y - center_ndc.y;

        // Disc edge at this row (may be imaginary for |dy| > radius)
        const f32 disc_hw = std::sqrt(std::max(0.0f, radius_ndc * radius_ndc - dy * dy));
        if (disc_hw < 1e-6f)
        {
            continue;
        }

        // Determine shadowed x range depending on waxing/waning:
        //   Waxing (bright RIGHT): shadow on LEFT, from disc-edge to terminator
        //   Waning (bright LEFT):  shadow on RIGHT, from terminator to disc-edge
        f32 x_start, x_end;
        if (waxing)
        {
            x_start = center_ndc.x - disc_hw;
            x_end   = center_ndc.x + t_offset;
        }
        else
        {
            x_start = center_ndc.x - t_offset;
            x_end   = center_ndc.x + disc_hw;
        }

        if (x_end > x_start)
        {
            lines.add_line(Vec2f{x_start, y}, Vec2f{x_end, y}, kMoonShadowColor);
        }
    }
}

// =================================================================
// NDC → pixel conversion
// =================================================================

Vec2f SolarSystemRenderer::ndc_to_pixel(Vec2f ndc, VkExtent2D viewport)
{
    const f32 px = (ndc.x + 1.0f) * 0.5f * static_cast<f32>(viewport.width);
    const f32 py = (ndc.y + 1.0f) * 0.5f * static_cast<f32>(viewport.height);
    return {px, py};
}

} // namespace parallax::rendering
