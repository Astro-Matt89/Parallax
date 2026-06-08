#include "ui/tabs/planetarium_tab.hpp"

#include "astro/time_system.hpp"
#include "core/logger.hpp"
#include "knowledge/knowledge_database.hpp"
#include "ui/font.hpp"
#include "ui/tabs/tab_render_helpers.hpp"
#include "universe/universe.hpp"
#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"

#include <SDL2/SDL_scancode.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace parallax::ui::tabs
{
    PlanetariumTab::PlanetariumTab(const vulkan::Context& context,
                                   vulkan::Swapchain& swapchain,
                                   VkRenderPass render_pass,
                                   const std::filesystem::path& shader_dir,
                                   universe::Universe& universe,
                                   const knowledge::KnowledgeDatabase& knowledge,
                                   BitmapFont& font,
                                   f64& julian_date,
                                   const astro::ObserverRegistry& observer_registry)
        : m_context(context)
        , m_swapchain(swapchain)
        , m_universe(universe)
        , m_knowledge(knowledge)
        , m_font(font)
        , m_julian_date(julian_date)
        , m_observer_registry(observer_registry)
        , m_frame_observer(observer_registry.get_active())
    {
        m_sky_background = std::make_unique<rendering::SkyBackground>(
            m_context,
            render_pass,
            shader_dir,
            m_swapchain.get_extent());

        m_starfield = std::make_unique<rendering::Starfield>(
            m_context,
            render_pass,
            shader_dir,
            300000);

        m_line_renderer = std::make_unique<rendering::LineRenderer>(
            m_context,
            render_pass,
            shader_dir);

        m_camera = std::make_unique<rendering::Camera>();

        const auto extent = m_swapchain.get_extent();
        m_viewport = {
            .x = 0,
            .y = 0,
            .width = extent.width,
            .height = extent.height
        };

        const std::filesystem::path star_names_path{"data/catalogs/star_names.csv"};
        if (m_selection.load_star_names(star_names_path))
        {
            PLX_CORE_INFO("Star names loaded: {} entries", m_selection.get_name_count());
        }
        else
        {
            PLX_CORE_WARN("Star names not found at {}", star_names_path.string());
        }

        const std::filesystem::path const_lines{"data/catalogs/constellation_lines.csv"};
        const std::filesystem::path const_names{"data/catalogs/constellation_names.csv"};
        if (m_constellations.load(const_lines, const_names))
        {
            m_constellations.resolve_via_universe(&m_universe);
        }
    }

    void PlanetariumTab::update(f64 delta_time)
    {
        static_cast<void>(delta_time);

        const shell::ViewportRect viewport = current_viewport();
        const i32 active_index = m_observer_registry.get_active_index();
        m_frame_observer = m_observer_registry.get_active();
        if (active_index != m_last_seen_active_index)
        {
            m_last_seen_active_index = active_index;
        }

        m_frame_atmosphere_on = atmosphere_effectively_on();
        m_frame_horizon_culling_on = m_atmosphere_on || !m_frame_observer.has_atmosphere;
        m_frame_lst = astro::TimeSystem::lmst(m_julian_date, m_frame_observer.longitude_rad);

        if (m_shell_tracking_target_id.has_value())
        {
            if (const auto tracked = m_universe.query_object(m_shell_tracking_target_id.value()); tracked.has_value())
            {
                const astro::HorizontalCoord hz = astro::Coordinates::equatorial_to_horizontal(
                    {tracked->ra, tracked->dec},
                    m_frame_observer,
                    m_frame_lst);
                m_camera->set_pointing(hz.alt, hz.az);
            }
        }

        m_universe.update(m_julian_date);

        if (!m_procedural_first_tick_logged)
        {
            PLX_CORE_INFO("Procedural provider active — master seed 0xA5735E5D, nside=64 cells");
            m_procedural_first_tick_logged = true;
        }

        const auto ss_bodies  = astro::SolarSystem::compute_all(m_julian_date);
        const auto moon_state = astro::SolarSystem::compute_moon_full(m_julian_date);

        const auto sun_hz = astro::Coordinates::equatorial_to_horizontal(
            ss_bodies.sun.equatorial,
            m_frame_observer,
            m_frame_lst);
        const auto moon_hz = astro::Coordinates::equatorial_to_horizontal(
            ss_bodies.moon.equatorial,
            m_frame_observer,
            m_frame_lst);

        m_sky_params.bortle_scale = m_frame_observer.bortle_scale;
        m_sky_params.sun_altitude_deg  = static_cast<f32>(sun_hz.alt * astro_constants::kRadToDeg);
        m_sky_params.sun_azimuth_deg   = static_cast<f32>(sun_hz.az * astro_constants::kRadToDeg);
        m_sky_params.moon_altitude_deg = static_cast<f32>(moon_hz.alt * astro_constants::kRadToDeg);
        m_sky_params.moon_azimuth_deg  = static_cast<f32>(moon_hz.az * astro_constants::kRadToDeg);
        m_sky_params.moon_illumination = moon_state.body.illumination;
        m_sky_params.atmosphere_enabled = m_frame_atmosphere_on;
        m_sun_altitude_deg = m_sky_params.sun_altitude_deg;

        m_sky_background->update_params(m_sky_params, *m_camera, viewport.aspect());
        m_frame_pointing = m_camera->get_pointing();
        m_frame_fov_rad = m_camera->get_fov_rad();
        m_frame_mag_limit = m_camera->get_magnitude_limit();
        m_frame_aspect_ratio = static_cast<f64>(viewport.aspect());

        const auto camera_eq = astro::Coordinates::horizontal_to_equatorial(
            m_frame_pointing,
            m_frame_observer,
            m_frame_lst);

        const f64 query_radius_deg = std::clamp(m_frame_fov_rad * 0.75 * astro_constants::kRadToDeg, 0.0, 180.0);

        m_universe.query_fov(
            camera_eq.ra,
            camera_eq.dec,
            query_radius_deg,
            m_frame_mag_limit,
            universe::QueryFlags::All,
            m_frame_objects);
        m_selection.update_from_objects(
            m_frame_objects,
            m_solar_system_renderer.get_screen_objects(),
            m_frame_observer,
            m_frame_lst,
            *m_camera,
            viewport);
    }

    void PlanetariumTab::render(VkCommandBuffer cmd, const shell::ViewportRect& viewport)
    {
        if (!viewport.is_valid())
        {
            return;
        }

        m_viewport = viewport;
        m_line_renderer->begin_frame();
        m_coord_grid.update(*m_camera, m_frame_observer, m_frame_lst, *m_line_renderer, m_font, viewport);

        m_starfield->begin_frame(m_frame_mag_limit);
        m_solar_system_renderer.begin_frame(*m_line_renderer, m_font, viewport, m_frame_horizon_culling_on);
        m_dso_renderer.begin_frame(*m_line_renderer, m_font, viewport);

        for (const auto& obj : m_frame_objects)
        {
            const auto hz = astro::Coordinates::equatorial_to_horizontal(
                {obj.ra, obj.dec},
                m_frame_observer,
                m_frame_lst);

            if (m_frame_horizon_culling_on && hz.alt < 0.0)
            {
                continue;
            }

            rendering::RenderStyle style = rendering::RenderStyle::Historical;
            if (!obj.is_real())
            {
                if (!m_knowledge.is_known(obj.id))
                {
                    continue;
                }

                style = m_knowledge.is_confirmed(obj.id)
                    ? rendering::RenderStyle::Confirmed
                    : rendering::RenderStyle::Candidate;
            }

            const auto screen_opt = astro::Coordinates::horizontal_to_screen(
                hz,
                m_frame_pointing,
                m_frame_fov_rad,
                m_frame_aspect_ratio);
            if (!screen_opt.has_value())
            {
                continue;
            }

            const Vec2f screen = screen_opt.value();
            switch (obj.type)
            {
                case universe::ObjectType::Star:
                case universe::ObjectType::ProceduralStar:
                    m_starfield->add_celestial_object(screen, obj, style);
                    break;

                case universe::ObjectType::SolarSystemBody:
                    m_solar_system_renderer.add_celestial_object(screen, hz.alt, hz.az, obj, style);
                    break;

                case universe::ObjectType::DeepSkyObject:
                    m_dso_renderer.add_celestial_object(screen, obj, style);
                    break;

                default:
                    break;
            }
        }

        m_starfield->end_frame();
        m_constellations.update(*m_camera, m_frame_observer, m_frame_lst, *m_line_renderer, m_font, viewport);
        m_horizon.update(*m_camera, *m_line_renderer, m_font, viewport);
        if (m_selection.has_selection())
        {
            m_selection.render_indicator(*m_line_renderer, viewport);
        }

        apply_viewport(cmd, viewport);
        m_sky_background->draw(cmd, viewport);
        m_starfield->draw(cmd);
        shell::apply_full_viewport_pane_scissor(cmd, m_swapchain.get_extent(), viewport);
        m_line_renderer->render(cmd);
        m_font.render(cmd, m_swapchain.get_extent());
    }

    void PlanetariumTab::on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport)
    {
        if (!viewport.is_valid() || !event.inside_viewport)
        {
            return;
        }

        m_viewport = viewport;
        const astro::ObserverLocation& observer = m_observer_registry.get_active();

        if (event.is_dragging)
        {
            const f64 fov = m_camera->get_fov_rad();
            const f64 sensitivity = fov / static_cast<f64>(std::max(viewport.width, 1u));
            const f64 delta_az = -static_cast<f64>(event.drag_delta.x) * sensitivity;
            const f64 delta_alt = -static_cast<f64>(event.drag_delta.y) * sensitivity;
            m_camera->pan(delta_az, delta_alt);
        }
        else if (event.was_click)
        {
            const f32 ndc_x = (2.0f * event.click_pos.x / static_cast<f32>(viewport.width)) - 1.0f;
            const f32 ndc_y = (2.0f * event.click_pos.y / static_cast<f32>(viewport.height)) - 1.0f;
            const Vec2f click_ndc{ndc_x, ndc_y};
            const f64 lst = astro::TimeSystem::lmst(m_julian_date, observer.longitude_rad);

            m_selection.try_select_from_objects(
                click_ndc,
                m_frame_objects,
                observer,
                lst,
                *m_camera,
                viewport);
        }

        if (event.scroll_delta != 0.0f)
        {
            const f64 zoom_factor = 1.0 - static_cast<f64>(event.scroll_delta) * 0.1;
            m_camera->zoom(zoom_factor);
        }
    }

    shell::TabId PlanetariumTab::get_id() const
    {
        return shell::TabId::Planetarium;
    }

    void PlanetariumTab::set_viewport(const shell::ViewportRect& viewport) noexcept
    {
        m_viewport = viewport;
    }

    void PlanetariumTab::handle_keyboard(const core::Input& input)
    {
        if (input.is_key_pressed(SDL_SCANCODE_RIGHTBRACKET) ||
            input.is_key_pressed(SDL_SCANCODE_PAGEDOWN))
        {
            m_camera->adjust_magnitude_limit(0.5f);
            PLX_CORE_INFO("Magnitude limit: {:.1f} (fainter)", m_camera->get_magnitude_limit());
        }

        if (input.is_key_pressed(SDL_SCANCODE_LEFTBRACKET) ||
            input.is_key_pressed(SDL_SCANCODE_PAGEUP))
        {
            m_camera->adjust_magnitude_limit(-0.5f);
            PLX_CORE_INFO("Magnitude limit: {:.1f} (brighter)", m_camera->get_magnitude_limit());
        }

        if (input.is_key_pressed(SDL_SCANCODE_C))
        {
            m_constellations.toggle_visible();
            PLX_CORE_INFO("Constellations {}", m_constellations.is_visible() ? "shown" : "hidden");
        }

        if (input.is_key_pressed(SDL_SCANCODE_G))
        {
            m_coord_grid.cycle_type();
            PLX_CORE_INFO("Coordinate grid: {}", m_coord_grid.get_type_name());
        }

        if (input.is_key_pressed(SDL_SCANCODE_D))
        {
            m_dso_renderer.toggle_visible();
            PLX_CORE_INFO("DSOs {}", m_dso_renderer.is_visible() ? "shown" : "hidden");
        }

        if (input.is_key_pressed(SDL_SCANCODE_P))
        {
            m_solar_system_renderer.toggle_visible();
            PLX_CORE_INFO("Solar System {}", m_solar_system_renderer.is_visible() ? "shown" : "hidden");
        }

        if (input.is_key_pressed(SDL_SCANCODE_O))
        {
            m_horizon.toggle_visible();
            PLX_CORE_INFO("Horizon {}", m_horizon.is_visible() ? "shown" : "hidden");
        }

        if (input.is_key_pressed(SDL_SCANCODE_A))
        {
            toggle_atmosphere();
        }

        if (input.is_key_pressed(SDL_SCANCODE_ESCAPE) && m_selection.has_selection())
        {
            m_selection.clear();
            PLX_CORE_INFO("Selection cleared");
        }

        if (input.is_key_pressed(SDL_SCANCODE_F) && m_selection.has_selection())
        {
            m_selection.set_tracking(!m_selection.is_tracking());
            PLX_CORE_INFO("Tracking {}", m_selection.is_tracking() ? "enabled" : "disabled");
        }

        if (input.is_key_pressed(SDL_SCANCODE_R))
        {
            m_camera->reset();
            PLX_CORE_INFO("Camera reset (MLIM {:.1f})", m_camera->get_magnitude_limit());
        }
    }

    void PlanetariumTab::set_fov(f64 fov_deg)
    {
        m_camera->set_fov(fov_deg);
    }

    f64 PlanetariumTab::get_fov_deg() const
    {
        return m_camera->get_fov_deg();
    }

    void PlanetariumTab::set_magnitude_limit(f32 magnitude_limit)
    {
        m_camera->set_magnitude_limit(magnitude_limit);
    }

    void PlanetariumTab::adjust_magnitude_limit(f32 delta)
    {
        m_camera->adjust_magnitude_limit(delta);
    }

    f32 PlanetariumTab::get_magnitude_limit() const
    {
        return m_camera->get_magnitude_limit();
    }

    void PlanetariumTab::toggle_constellations()
    {
        m_constellations.toggle_visible();
    }

    void PlanetariumTab::cycle_grid()
    {
        m_coord_grid.cycle_type();
    }

    void PlanetariumTab::toggle_dso()
    {
        m_dso_renderer.toggle_visible();
    }

    void PlanetariumTab::toggle_solar_system()
    {
        m_solar_system_renderer.toggle_visible();
    }

    void PlanetariumTab::toggle_horizon()
    {
        m_horizon.toggle_visible();
    }

    void PlanetariumTab::toggle_atmosphere()
    {
        m_atmosphere_on = !m_atmosphere_on;
        const astro::ObserverLocation& observer = m_observer_registry.get_active();
        if (m_atmosphere_on && !observer.has_atmosphere)
        {
            spdlog::debug("Atmosphere preference enabled at vacuum site '{}'; effect remains disabled",
                          observer.name);
        }

        PLX_CORE_INFO("Atmosphere: {}", atmosphere_effectively_on() ? "ON (twilight gradient)" : "OFF (pure black)");
    }

    void PlanetariumTab::toggle_tracking()
    {
        if (!m_selection.has_selection())
        {
            return;
        }

        m_selection.set_tracking(!m_selection.is_tracking());
    }

    void PlanetariumTab::clear_selection()
    {
        m_selection.clear();
    }

    void PlanetariumTab::center_on(const u64 object_id)
    {
        const auto target = m_universe.query_object(object_id);
        if (!target.has_value())
        {
            PLX_CORE_WARN("PlanetariumTab::center_on could not find object {}", object_id);
            return;
        }

        const astro::ObserverLocation& observer = m_observer_registry.get_active();
        const f64 lst = astro::TimeSystem::lmst(m_julian_date, observer.longitude_rad);
        const astro::HorizontalCoord hz = astro::Coordinates::equatorial_to_horizontal(
            {target->ra, target->dec},
            observer,
            lst);
        m_camera->set_pointing(hz.alt, hz.az);
    }

    void PlanetariumTab::start_tracking(const u64 object_id)
    {
        m_shell_tracking_target_id = object_id;
        center_on(object_id);
    }

    void PlanetariumTab::set_atmosphere(bool on)
    {
        m_atmosphere_on = on;
    }

    void PlanetariumTab::set_bortle_scale(f32 bortle_scale)
    {
        static_cast<void>(bortle_scale);
    }

    bool PlanetariumTab::is_atmosphere_on() const noexcept
    {
        return m_atmosphere_on;
    }

    bool PlanetariumTab::atmosphere_effectively_on() const noexcept
    {
        // TopBar consumes this via Shell in Task 9.11:
        //   state.vacuum_site   = active.parent_body != ParentBody::Earth;
        //   state.atmosphere_on = atmosphere_effectively_on();
        return m_atmosphere_on && m_observer_registry.get_active().has_atmosphere;
    }

    f32 PlanetariumTab::get_bortle_scale() const noexcept
    {
        return m_observer_registry.get_active().bortle_scale;
    }

    f32 PlanetariumTab::get_sun_altitude_deg() const noexcept
    {
        return m_sun_altitude_deg;
    }

    bool PlanetariumTab::constellations_visible() const noexcept
    {
        return m_constellations.is_visible();
    }

    overlay::GridType PlanetariumTab::grid_type() const noexcept
    {
        return m_coord_grid.get_type();
    }

    const char* PlanetariumTab::grid_type_name() const
    {
        return m_coord_grid.get_type_name();
    }

    bool PlanetariumTab::dso_visible() const noexcept
    {
        return m_dso_renderer.is_visible();
    }

    bool PlanetariumTab::solar_system_visible() const noexcept
    {
        return m_solar_system_renderer.is_visible();
    }

    bool PlanetariumTab::horizon_visible() const noexcept
    {
        return m_horizon.is_visible();
    }

    u32 PlanetariumTab::visible_star_count() const
    {
        return m_starfield->get_visible_count();
    }

    bool PlanetariumTab::has_selection() const noexcept
    {
        return m_selection.has_selection();
    }

    const rendering::Camera& PlanetariumTab::get_camera() const noexcept
    {
        return *m_camera;
    }

    const Selection& PlanetariumTab::get_selection() const noexcept
    {
        return m_selection;
    }

    std::span<const universe::CelestialObject> PlanetariumTab::get_frame_objects() const noexcept
    {
        return m_frame_objects;
    }

    void PlanetariumTab::apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect& viewport) const
    {
        VkViewport vk_viewport{};
        vk_viewport.x = static_cast<f32>(viewport.x);
        vk_viewport.y = static_cast<f32>(viewport.y);
        vk_viewport.width = static_cast<f32>(viewport.width);
        vk_viewport.height = static_cast<f32>(viewport.height);
        vk_viewport.minDepth = 0.0f;
        vk_viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vk_viewport);

        VkRect2D scissor{};
        scissor.offset = {
            static_cast<int32_t>(viewport.x),
            static_cast<int32_t>(viewport.y)
        };
        scissor.extent = {
            viewport.width,
            viewport.height
        };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    shell::ViewportRect PlanetariumTab::current_viewport() const noexcept
    {
        if (m_viewport.is_valid())
        {
            return m_viewport;
        }

        const auto extent = m_swapchain.get_extent();
        return {
            .x = 0,
            .y = 0,
            .width = extent.width,
            .height = extent.height
        };
    }
}
