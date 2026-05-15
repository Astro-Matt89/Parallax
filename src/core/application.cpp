/// @file application.cpp
/// @brief Application implementation — skychart mode.
///
/// SPRINT 08 Task 8.9: Knowledge-aware rendering. Procedural objects skipped unless
///                     discovered; Confirmed/Candidate styled differently.
/// SPRINT 07 Task 7.7: Rewired through Universe facade.
/// SPRINT 06 Task 6.7: Canonical frame-loop order documented in update_simulation().
/// SPRINT 04 Task 4.7: Full overlay integration.
/// Render order:
///   1. Sky background
///   2. Coordinate grid (behind stars)
///   3. Starfield (additive)
///   4. Solar System bodies (Sun, Moon, planets)
///   5. Constellation lines + labels (over stars)
///   6. DSO icons + labels (over stars)
///   7. Horizon line + cardinal markers (over everything except HUD)
///   8. HUD (always on top)

#include "core/application.hpp"

#include "rendering/render_style.hpp"                    // ← SPRINT 08 Task 8.9

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>

namespace
{

void check_vk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        PLX_CORE_CRITICAL("Vulkan error in {}: VkResult = {}", operation, static_cast<int>(result));
        std::abort();
    }
}

} // anonymous namespace

namespace parallax::core
{

Application::Application()
{
    init();
}

Application::~Application()
{
    shutdown();
}

void Application::run()
{
    PLX_CORE_INFO("Entering main loop...");
    main_loop();
    PLX_CORE_INFO("Main loop exited");
}

// =================================================================
// Initialization
// =================================================================

void Application::init()
{
    // 1. Window
    m_window = std::make_unique<Window>(WindowConfig{.title = "Parallax", .width = 1280, .height = 720});

    // 2. Input
    m_input = std::make_unique<Input>();
    m_window->set_event_callback([this](const SDL_Event& event) {
        m_input->process_event(event);
    });

    // 3. Vulkan context
    m_context = std::make_unique<vulkan::Context>(
        vulkan::ContextConfig{.app_name = "Parallax", .enable_validation = true},
        *m_window);

    // 4. Swapchain
    m_swapchain = std::make_unique<vulkan::Swapchain>(
        *m_context, m_window->get_width(), m_window->get_height());

    // 5. Pipeline
    std::filesystem::path shader_dir{PLX_SHADER_DIR};
    PLX_CORE_INFO("Shader directory: {}", shader_dir.string());
    m_pipeline = std::make_unique<vulkan::Pipeline>(*m_context, *m_swapchain, shader_dir);

    // 6. Sky background
    m_sky_background = std::make_unique<rendering::SkyBackground>(
        *m_context, m_pipeline->get_render_pass(), shader_dir, m_swapchain->get_extent());

    // 7. Starfield renderer (skychart mode — no atmosphere parameter)
    //    Buffer increased to 300k for Tycho-2 at high MLIM           ← SPRINT 05 Task 5.0
    m_starfield = std::make_unique<rendering::Starfield>(
        *m_context, m_pipeline->get_render_pass(), shader_dir, 300000);

    // 7b. Line renderer for overlays (constellations, grids, horizon)  ← SPRINT 04 Task 4.1
    m_line_renderer = std::make_unique<rendering::LineRenderer>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 8. Camera
    m_camera = std::make_unique<rendering::Camera>();

    // 9. HUD overlay
    m_hud = std::make_unique<ui::Hud>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 9b. PanelSystem — batched panel backgrounds           ← SPRINT 05 Task 5.1
    m_panel_system.init(*m_context, m_pipeline->get_render_pass(), shader_dir);

    // 9c. Toolbar                                           ← SPRINT 05 Task 5.3
    {
        ui::ToolbarCallbacks cb;
        cb.toggle_constellations = [this]() { m_constellations.toggle_visible(); };
        cb.toggle_stars          = [this]() { /* TODO: add Starfield visibility toggle */ };
        cb.toggle_dso            = [this]() { m_dso_renderer.toggle_visible(); };
        cb.cycle_grid            = [this]() { m_coord_grid.cycle_type(); };
        cb.toggle_horizon        = [this]() { m_horizon.toggle_visible(); };
        cb.toggle_atmosphere     = [this]() { toggle_atmosphere(); };  // ← SPRINT 06 Task 6.7
        cb.time_reverse          = [this]()
        {
            if (m_time_scale >= 0.0) { m_time_scale = -1.0; }
            else { m_time_scale *= 2.0; }
        };
        cb.time_pause_toggle = [this]()
        {
            m_time_scale = (m_time_scale != 0.0) ? 0.0 : 1.0;
        };
        cb.time_forward = [this]()
        {
            if (m_time_scale <= 0.0) { m_time_scale = 1.0; }
            else { m_time_scale *= 2.0; }
        };
        cb.time_reset_now = [this]()
        {
            m_julian_date = astro::TimeSystem::now_as_jd();
            m_time_scale = 1.0;
        };
        cb.set_fov = [this](f64 fov_deg) { m_camera->set_fov(fov_deg); };
        m_toolbar.init(cb);
    }

    // 9d. SidePanel                                         ← SPRINT 05 Task 5.4
    {
        ui::SidePanelCallbacks cb;
        cb.set_location = [this](f64 lat_deg, f64 lon_deg, f64 elev_m, f32 bortle)
        {
            m_observer.latitude_rad  = glm::radians(lat_deg);
            m_observer.longitude_rad = glm::radians(lon_deg);
            m_elevation_m            = elev_m;
            m_sky_params.bortle_scale = bortle;
            PLX_CORE_INFO("Observer location set: {:.2f}N {:.2f}E {:.0f}m Bortle {}",
                          lat_deg, lon_deg, elev_m, static_cast<int>(bortle));
        };
        cb.set_bortle = [this](f32 bortle)
        {
            m_sky_params.bortle_scale = bortle;
            PLX_CORE_INFO("Bortle scale: {}", static_cast<int>(bortle));
        };
        cb.set_magnitude_limit = [this](f32 mag)
        {
            m_camera->set_magnitude_limit(mag);
            PLX_CORE_INFO("Magnitude limit: {:.1f}", mag);
        };
        cb.set_time_scale = [this](f64 scale)
        {
            m_time_scale = scale;
            PLX_CORE_INFO("Time scale: x{}", scale);
        };
        m_side_panel.init(cb);
    }

    // 9e. InfoPanel                                         ← SPRINT 05 Task 5.5
    {
        ui::InfoPanelCallbacks cb;
        cb.track = [this]()
        {
            m_selection.set_tracking(!m_selection.is_tracking());
            PLX_CORE_INFO("Tracking {}", m_selection.is_tracking() ? "enabled" : "disabled");
        };
        cb.goto_object = [this]() { /* TODO: future telescope slew command */ };
        m_info_panel.init(cb);
    }

    // 9f. Selection — load star names                      ← SPRINT 05 Task 5.5
    {
        const std::filesystem::path star_names_path{"data/catalogs/star_names.csv"};
        if (m_selection.load_star_names(star_names_path))
        {
            PLX_CORE_INFO("Star names loaded: {} entries", m_selection.get_name_count());
        }
        else
        {
            PLX_CORE_WARN("Star names not found at {}", star_names_path.string());
        }
    }

    // =================================================================
    // 10. Universe facade — replaces all direct catalog loading.
    //     ← SPRINT 07 Task 7.7
    // =================================================================
    {
        m_universe = std::make_unique<universe::Universe>();

        const std::filesystem::path data_dir{"data/catalogs"};
        if (!m_universe->load_catalogs(data_dir))
        {
            PLX_CORE_CRITICAL("Universe: failed to load catalogs from '{}'", data_dir.string());
            std::abort();
        }


        // TODO: wire master seed to config when config system supports it.
        // Using a fixed seed until then so procedural content is reproducible.
        static constexpr std::uint64_t kMasterSeed = 0xA5735E5DULL;
        m_universe->init_procedural(kMasterSeed);

        PLX_CORE_INFO("Universe initialized: {} real objects",
                      m_universe->get_real_object_count());
    }

    // 10c. KnowledgeDatabase — default-constructed (empty).                 ← SPRINT 08 Task 8.9
    //      initialize_from_historical_catalogs() is called in Task 8.11 once
    //      the full session/analysis pipeline is wired.  For now the DB is
    //      empty: all real catalog objects render as Historical (is_real()),
    //      and no procedural objects appear (none discovered yet).
    m_knowledge = std::make_unique<knowledge::KnowledgeDatabase>();
    PLX_CORE_INFO("KnowledgeDatabase initialized (empty — historical rendering active)");
    // TODO(Sprint 08 Task 8.11): Call initialize_from_historical_catalogs() here once
    //                             the full session/analysis pipeline is wired.

    // 10b. Load constellation overlay — resolve via Universe                ← SPRINT 04 Task 4.2
    {
        const std::filesystem::path const_lines{"data/catalogs/constellation_lines.csv"};
        const std::filesystem::path const_names{"data/catalogs/constellation_names.csv"};
        if (m_constellations.load(const_lines, const_names))
        {
            m_constellations.resolve_via_universe(m_universe.get());
        }
    }

    // 11. Observer: La Palma, Canary Islands
    m_observer = astro::ObserverLocation{
        .latitude_rad  = glm::radians(28.76),
        .longitude_rad = glm::radians(-17.89),
    };

    // 12. Simulation time
    m_julian_date = astro::TimeSystem::now_as_jd();
    m_time_scale = 1.0;
    {
        const auto dt = astro::TimeSystem::from_julian_date(m_julian_date);
        PLX_CORE_INFO("Simulation start: JD {:.6f} ({:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:04.1f} UTC)",
                      m_julian_date, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        PLX_CORE_INFO("Observer: La Palma ({:.2f}N, {:.2f}W)",
                      glm::degrees(m_observer.latitude_rad),
                      -glm::degrees(m_observer.longitude_rad));
    }

    // 13. Sky parameters — ephemeris fields (sun/moon) are set each frame in update_simulation.
    m_sky_params = rendering::SkyParams{
        .bortle_scale = 4.0f,
        // sun_altitude_deg, sun_azimuth_deg, moon_altitude_deg, moon_azimuth_deg,
        // moon_illumination, atmosphere_enabled — all use struct defaults; overwritten each frame.
    };

    // 14. Atmosphere model: KEPT for future imaging mode, not used in skychart
    m_atmosphere.set_params(astro::AtmosphereParams{
        .pressure_mbar = 1013.25f,
        .temperature_c = 15.0f,
        .extinction_coeff = 0.20f,
        .bortle_scale = m_sky_params.bortle_scale,
    });
    PLX_CORE_INFO("Atmosphere model initialized (dormant — skychart mode)");

    // 15-17. Command pool, sync, frame time
    create_command_pool();
    create_command_buffers();
    create_sync_objects();
    m_last_frame_time = std::chrono::steady_clock::now();

    // Log initial overlay states
    PLX_CORE_INFO("Overlays: CONST={} GRID={} DSO={} HORIZ={}",
                  m_constellations.is_visible() ? "ON" : "OFF",
                  m_coord_grid.get_type_name(),
                  m_dso_renderer.is_visible() ? "ON" : "OFF",
                  m_horizon.is_visible() ? "ON" : "OFF");

    PLX_CORE_INFO("Application initialized — skychart mode, MLIM {:.1f}",
                  m_camera->get_magnitude_limit());
}

// =================================================================
// Shutdown
// =================================================================

void Application::shutdown()
{
    if (!m_context) return;

    m_context->wait_idle();
    destroy_sync_objects();

    if (m_command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_context->get_device(), m_command_pool, nullptr);
        m_command_pool = VK_NULL_HANDLE;
    }

    m_panel_system.destroy();   // ← SPRINT 05 Task 5.1 (destroy before HUD)
    m_hud.reset();
    m_line_renderer.reset();    // ← SPRINT 04 Task 4.1 (destroy before starfield)
    m_starfield.reset();
    m_sky_background.reset();
    m_pipeline.reset();
    m_swapchain.reset();
    m_context.reset();

    if (m_window) m_window->set_event_callback(nullptr);
    m_input.reset();
    m_window.reset();
}

// =================================================================
// Main loop
// =================================================================

void Application::main_loop()
{
    while (!m_window->should_close())
    {
        m_input->new_frame();
        m_window->poll_events();

        if (m_window->was_resized()) m_framebuffer_resized = true;
        if (m_window->get_width() == 0 || m_window->get_height() == 0) continue;

        auto now = std::chrono::steady_clock::now();
        const f64 delta_time_sec = std::chrono::duration<f64>(now - m_last_frame_time).count();
        m_last_frame_time = now;
        const f64 clamped_dt = std::min(delta_time_sec, 0.1);
        m_delta_time = delta_time_sec;

        process_input();
        update_simulation(clamped_dt);
        draw_frame();
    }

    m_context->wait_idle();
}

// =================================================================
// Atmosphere toggle API                           ← SPRINT 06 Task 6.7
// =================================================================

void Application::toggle_atmosphere()
{
    m_atmosphere_on = !m_atmosphere_on;
    PLX_CORE_INFO("Atmosphere: {}", m_atmosphere_on ? "ON (twilight gradient)" : "OFF (pure black)");
}

void Application::set_atmosphere(bool on)
{
    m_atmosphere_on = on;
}

// =================================================================
// Input processing — mouse priority + key bindings      ← SPRINT 05 Task 5.6
//
// Mouse interaction priority:
//   1. Mouse over UI panel → UI handles it
//   2. Click on sky (not drag) → object selection
//   3. Drag on sky → camera pan
//   4. Scroll on sky → zoom
//
// Cursor styles:
//   Arrow     → default / not over anything interactive
//   Hand      → over a clickable UI element
//   Crosshair → over the sky (ready to select)
//   SizeAll   → actively dragging the sky (panning)
// =================================================================

void Application::process_input()
{
    const Vec2f mouse_pos = m_input->get_mouse_position();
    const VkExtent2D viewport = m_swapchain->get_extent();

    // =================================================================
    // Step 1: Determine if mouse is over any UI panel
    // =================================================================
    const bool mouse_over_ui = m_panel_system.is_mouse_over_ui(mouse_pos)
                            || m_toolbar.is_mouse_over(mouse_pos)
                            || m_side_panel.is_mouse_over(mouse_pos)
                            || m_info_panel.is_mouse_over(mouse_pos);

    // =================================================================
    // Step 2: Route mouse input based on priority
    // =================================================================
    if (mouse_over_ui)
    {
        // --- Priority 1: UI panels consume mouse input ---
        m_panel_system.process_input(*m_input, mouse_pos);
        m_input->set_cursor(CursorStyle::Hand);
    }
    else
    {
        // --- Sky interaction ---

        // Priority 3: Drag on sky → camera pan
        if (m_input->is_mouse_dragging())
        {
            const auto drag = m_input->get_mouse_drag_delta();
            const f64 fov = m_camera->get_fov_rad();
            const f64 sensitivity = fov / static_cast<f64>(m_window->get_width());

            const f64 delta_az  = -static_cast<f64>(drag.x) * sensitivity;
            const f64 delta_alt = -static_cast<f64>(drag.y) * sensitivity;
            m_camera->pan(delta_az, delta_alt);

            m_input->set_cursor(CursorStyle::SizeAll);
        }
        // Priority 2: Click on sky (not drag) → object selection
        else if (m_input->was_click())
        {
            const Vec2f click_pos = m_input->get_click_position();

            const f32 ndc_x = (2.0f * click_pos.x / static_cast<f32>(viewport.width)) - 1.0f;
            const f32 ndc_y = (2.0f * click_pos.y / static_cast<f32>(viewport.height)) - 1.0f;
            const Vec2f click_ndc = {ndc_x, ndc_y};

            m_selection.try_select_from_objects(
                click_ndc,
                m_frame_objects,
                m_observer,
                astro::TimeSystem::lmst(m_julian_date, m_observer.longitude_rad),
                *m_camera,
                viewport);

            if (m_selection.has_selection())
            {
                const auto& sel = m_selection.get_selection();
                if (sel.type == ui::SelectedObjectType::Star)
                {
                    if (sel.is_procedural)
                    {
                        PLX_CORE_INFO("Selected procedural star: {} mag {:.2f} ({:.4f}, {:.4f})",
                                      sel.designation, sel.mag_v, sel.ra_rad, sel.dec_rad);
                    }
                    else
                    {
                        PLX_CORE_INFO("Selected star: HIP {} mag {:.2f} ({:.4f}, {:.4f})",
                                      sel.hip_id, sel.mag_v, sel.ra_rad, sel.dec_rad);
                    }
                }
                else if (sel.type == ui::SelectedObjectType::Dso)
                {
                    PLX_CORE_INFO("Selected DSO: {}", sel.designation);
                }
                else if (sel.type == ui::SelectedObjectType::SolarSystem)
                {
                    PLX_CORE_INFO("Selected Solar System body: {} (body_id {})",
                                  sel.body_name, sel.body_id);
                }
            }
            else
            {
                PLX_CORE_TRACE("Click on sky: no object found near cursor");
            }

            m_input->set_cursor(CursorStyle::Crosshair);
        }
        else
        {
            // Hovering over sky, not dragging or clicking
            m_input->set_cursor(CursorStyle::Crosshair);
        }

        // Priority 4: Scroll on sky → camera zoom
        const f32 scroll = m_input->get_scroll_delta();
        if (scroll != 0.0f)
        {
            const f64 zoom_factor = 1.0 - static_cast<f64>(scroll) * 0.1;
            m_camera->zoom(zoom_factor);
        }
    }

    // =================================================================
    // Keyboard bindings (always active regardless of mouse state)
    // =================================================================

    if (m_input->is_key_pressed(SDL_SCANCODE_RIGHTBRACKET) ||
        m_input->is_key_pressed(SDL_SCANCODE_PAGEDOWN))
    {
        m_camera->adjust_magnitude_limit(0.5f);
        PLX_CORE_INFO("Magnitude limit: {:.1f} (fainter)", m_camera->get_magnitude_limit());
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_LEFTBRACKET) ||
        m_input->is_key_pressed(SDL_SCANCODE_PAGEUP))
    {
        m_camera->adjust_magnitude_limit(-0.5f);
        PLX_CORE_INFO("Magnitude limit: {:.1f} (brighter)", m_camera->get_magnitude_limit());
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_1)) { m_time_scale = 1.0;     PLX_CORE_INFO("Time scale: x1"); }
    if (m_input->is_key_pressed(SDL_SCANCODE_2)) { m_time_scale = 10.0;    PLX_CORE_INFO("Time scale: x10"); }
    if (m_input->is_key_pressed(SDL_SCANCODE_3)) { m_time_scale = 100.0;   PLX_CORE_INFO("Time scale: x100"); }
    if (m_input->is_key_pressed(SDL_SCANCODE_4)) { m_time_scale = 1000.0;  PLX_CORE_INFO("Time scale: x1000"); }
    if (m_input->is_key_pressed(SDL_SCANCODE_5)) { m_time_scale = 10000.0; PLX_CORE_INFO("Time scale: x10000"); }

    if (m_input->is_key_pressed(SDL_SCANCODE_0) ||
        m_input->is_key_pressed(SDL_SCANCODE_SPACE))
    {
        if (m_time_scale != 0.0) { m_time_scale = 0.0; PLX_CORE_INFO("Time paused"); }
        else { m_time_scale = 1.0; PLX_CORE_INFO("Time resumed: x1"); }
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_MINUS))
    {
        if (m_time_scale == 0.0) m_time_scale = -1.0;
        else if (m_time_scale > 0.0) m_time_scale = -m_time_scale;
        PLX_CORE_INFO("Time scale: x{}", static_cast<int>(m_time_scale));
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_EQUALS))
    {
        m_julian_date = astro::TimeSystem::now_as_jd();
        m_time_scale = 1.0;
        const auto dt = astro::TimeSystem::from_julian_date(m_julian_date);
        PLX_CORE_INFO("Time reset to now: {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02.0f} UTC (x1)",
                      dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    }

    // UI controls
    if (m_input->is_key_pressed(SDL_SCANCODE_H))
    {
        m_hud->toggle_visible();
        PLX_CORE_INFO("HUD {}", m_hud->is_visible() ? "shown" : "hidden");
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_T))
    {
        m_hud->toggle_time_format();
        const char* format_names[] = {"UTC", "LST", "JD"};
        PLX_CORE_INFO("Time display: {}", format_names[static_cast<int>(m_hud->get_time_format())]);
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_B))
    {
        f32 bortle = m_sky_params.bortle_scale + 1.0f;
        if (bortle > 9.0f) bortle = 1.0f;
        m_sky_params.bortle_scale = bortle;

        auto atmo_params = m_atmosphere.get_params();
        atmo_params.bortle_scale = bortle;
        m_atmosphere.set_params(atmo_params);

        PLX_CORE_INFO("Bortle scale: {}", static_cast<int>(bortle));
    }

    // Overlay toggles
    if (m_input->is_key_pressed(SDL_SCANCODE_C))
    {
        m_constellations.toggle_visible();
        PLX_CORE_INFO("Constellations {}",
                      m_constellations.is_visible() ? "shown" : "hidden");
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_G))
    {
        m_coord_grid.cycle_type();
        PLX_CORE_INFO("Coordinate grid: {}", m_coord_grid.get_type_name());
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_D))
    {
        m_dso_renderer.toggle_visible();
        PLX_CORE_INFO("DSOs {}", m_dso_renderer.is_visible() ? "shown" : "hidden");
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_P))
    {
        m_solar_system_renderer.toggle_visible();
        PLX_CORE_INFO("Solar System {}", m_solar_system_renderer.is_visible() ? "shown" : "hidden");
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_O))
    {
        m_horizon.toggle_visible();
        PLX_CORE_INFO("Horizon {}", m_horizon.is_visible() ? "shown" : "hidden");
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_A))
    {
        toggle_atmosphere();
    }

    // Selection: Escape clears first, then closes window
    if (m_input->is_key_pressed(SDL_SCANCODE_ESCAPE))
    {
        if (m_selection.has_selection())
        {
            m_selection.clear();
            PLX_CORE_INFO("Selection cleared");
        }
        else
        {
            m_window->request_close();
        }
    }

    // F — Toggle tracking on selected object
    if (m_input->is_key_pressed(SDL_SCANCODE_F))
    {
       if (m_selection.has_selection())
       {
           m_selection.set_tracking(!m_selection.is_tracking());
           PLX_CORE_INFO("Tracking {}",
                         m_selection.is_tracking() ? "enabled" : "disabled");
       }
   }

    // Camera reset
    if (m_input->is_key_pressed(SDL_SCANCODE_R))
    {
        m_camera->reset();
        PLX_CORE_INFO("Camera reset (MLIM {:.1f})", m_camera->get_magnitude_limit());
    }
}

// =================================================================
// Simulation update — Universe path (Task 7.7)
//
// CANONICAL FRAME LOOP ORDER:
//
//  1.  Advance simulation clock → JD, LST.
//  2.  Universe update (ephemeris + time-dependent state).
//  3.  Compute Sun/Moon Alt/Az for sky background UBO.
//  4.  Update sky background.
//  5.  query_fov → m_frame_objects.
//  6.  Per-object: equatorial_to_horizontal, horizon cull, project to screen,
//      dispatch to renderer by ObjectType.
//  7.  Constellations, grid, horizon.
//  8.  Selection refresh.
//  9.  Panels, HUD.
// 10.  Submit / present.
// =================================================================

void Application::update_simulation(f64 delta_time_sec)
{
    // Advance Julian Date
    m_julian_date += (delta_time_sec * m_time_scale) / 86400.0;

    // Compute Local Sidereal Time
    const f64 lst = astro::TimeSystem::lmst(m_julian_date, m_observer.longitude_rad);

    // --- Universe update --- (must be before query_fov)
    m_universe->update(m_julian_date);

    // --- Procedural first-tick log ---
    if (!m_procedural_first_tick_logged)
    {
        PLX_CORE_INFO("Procedural provider active — master seed 0xA5735E5D, nside=64 cells");
        m_procedural_first_tick_logged = true;
    }

    // --- Update sky parameters with live Sun/Moon Alt/Az (same as before) ---
    {
        const auto ss_bodies  = astro::SolarSystem::compute_all(m_julian_date);
        const auto moon_state = astro::SolarSystem::compute_moon_full(m_julian_date);

        const auto sun_hz  = astro::Coordinates::equatorial_to_horizontal(
            ss_bodies.sun.equatorial, m_observer, lst);
        const auto moon_hz = astro::Coordinates::equatorial_to_horizontal(
            ss_bodies.moon.equatorial, m_observer, lst);

        m_sky_params.sun_altitude_deg  = static_cast<f32>(sun_hz.alt  * astro_constants::kRadToDeg);
        m_sky_params.sun_azimuth_deg   = static_cast<f32>(sun_hz.az   * astro_constants::kRadToDeg);
        m_sky_params.moon_altitude_deg = static_cast<f32>(moon_hz.alt * astro_constants::kRadToDeg);
        m_sky_params.moon_azimuth_deg  = static_cast<f32>(moon_hz.az  * astro_constants::kRadToDeg);
        m_sky_params.moon_illumination = moon_state.body.illumination;
        m_sky_params.atmosphere_enabled = m_atmosphere_on;

        m_sun_altitude_deg = m_sky_params.sun_altitude_deg;
    }

    // Update sky background (visual context only)
    m_sky_background->update_params(m_sky_params, *m_camera);

    // Clear line renderer for this frame
    m_line_renderer->begin_frame();

    const VkExtent2D viewport = m_swapchain->get_extent();

    // Coordinate grid (submit first — it goes behind overlays in the line batch)
    m_coord_grid.update(*m_camera, m_observer, lst,
                        *m_line_renderer, m_hud->get_font(), viewport);

    // ---  query_fov → m_frame_objects  ---
    const auto pointing  = m_camera->get_pointing();
    const f64  fov_rad   = m_camera->get_fov_rad();
    const f32  mag_limit = m_camera->get_magnitude_limit();

    // Convert camera pointing (Alt/Az) → RA/Dec for the FOV query
    const auto camera_eq = astro::Coordinates::horizontal_to_equatorial(
        pointing, m_observer, lst);

    // Query radius = FOV half-angle (gnomonic), padded by 25 %
    const f64 query_radius_deg = glm::degrees(fov_rad * 0.75);

    m_universe->query_fov(camera_eq.ra, camera_eq.dec,
                          query_radius_deg, mag_limit,
                          universe::QueryFlags::All,
                          m_frame_objects);

    // --- Begin renderer frames ---
    m_starfield->begin_frame(mag_limit);
    m_solar_system_renderer.begin_frame(*m_line_renderer, m_hud->get_font(), viewport, m_atmosphere_on);
    m_dso_renderer.begin_frame(*m_line_renderer, m_hud->get_font(), viewport);

    // --- Dispatch per-frame objects ---
    for (const auto& obj : m_frame_objects)
    {
        const auto hz = astro::Coordinates::equatorial_to_horizontal(
            {obj.ra, obj.dec}, m_observer, lst);

        if (m_atmosphere_on && hz.alt < 0.0)
        {
            continue;  // Below horizon when atmosphere culling is on
        }

        // -----------------------------------------------------------------
        // Knowledge-aware filter                              ← Task 8.9
        //
        // Real catalog objects (is_real()) always render as Historical.
        // Procedural objects are skipped unless the player has discovered
        // them (is_known).  Discovered procedural objects render as
        // Confirmed (≥ 2 independent detections) or Candidate (1 detection).
        // -----------------------------------------------------------------
        rendering::RenderStyle style = rendering::RenderStyle::Historical;

        if (!obj.is_real())
        {
            if (!m_knowledge->is_known(obj.id))
            {
                continue;  // Undiscovered procedural object — never visible
            }

            style = m_knowledge->is_confirmed(obj.id)
                        ? rendering::RenderStyle::Confirmed
                        : rendering::RenderStyle::Candidate;
        }

        const auto screen_opt = astro::Coordinates::horizontal_to_screen(hz, pointing, fov_rad);
        if (!screen_opt.has_value())
        {
            continue;  // Outside FOV
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
                break; // Galaxy / ProceduralDso — not yet rendered
        }
    }

    // Finalise starfield GPU upload
    m_starfield->end_frame();

    // --- Constellation lines + labels (over stars) ---
    m_constellations.update(*m_camera, m_observer, lst,
                            *m_line_renderer, m_hud->get_font(), viewport);

    // --- Horizon line + cardinal markers ---
    m_horizon.update(*m_camera, *m_line_renderer, m_hud->get_font(), viewport);

    // --- Selection — refresh screen position + Alt/Az each frame ---
    m_selection.update_from_objects(m_frame_objects,
                                    m_solar_system_renderer.get_screen_objects(),
                                    m_observer, lst, *m_camera);

    // --- Selection indicator ---
    if (m_selection.has_selection())
    {
        m_selection.render_indicator(*m_line_renderer, viewport);
    }

    // --- Toolbar update ---
    {
        const ui::ToolbarState toolbar_state{
            .constellations_visible = m_constellations.is_visible(),
            .stars_visible          = true,
            .dso_visible            = m_dso_renderer.is_visible(),
            .grid_visible           = m_coord_grid.get_type() != overlay::GridType::None,
            .horizon_visible        = m_horizon.is_visible(),
            .atmosphere_on          = m_atmosphere_on,
            .time_scale             = m_time_scale,
            .time_paused            = (m_time_scale == 0.0),
            .fov_deg                = m_camera->get_fov_deg(),
            .magnitude_limit        = m_camera->get_magnitude_limit(),
        };
        m_toolbar.update(
            m_input->get_mouse_position(),
            m_input->was_click(),
            m_input->is_left_button_down(),
            static_cast<f32>(delta_time_sec),
            viewport.width, viewport.height,
            toolbar_state);
    }

    // --- SidePanel update ---
    {
        const auto dt_utc = astro::TimeSystem::from_julian_date(m_julian_date);
        const auto utc_str = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
            dt_utc.year, dt_utc.month, dt_utc.day,
            dt_utc.hour, dt_utc.minute, static_cast<i32>(dt_utc.second));

        const f64 lst_hours = lst * astro_constants::kRadToHour;
        const i32 lst_h = static_cast<i32>(lst_hours);
        const f64 lst_m_frac = (lst_hours - static_cast<f64>(lst_h)) * 60.0;
        const i32 lst_m = static_cast<i32>(lst_m_frac);
        const i32 lst_s = static_cast<i32>((lst_m_frac - static_cast<f64>(lst_m)) * 60.0);
        const auto lst_str = std::format("{:02d}h {:02d}m {:02d}s", lst_h, lst_m, lst_s);

        const ui::SidePanelState side_state{
            .latitude_deg    = m_observer.latitude_rad  * astro_constants::kRadToDeg,
            .longitude_deg   = m_observer.longitude_rad * astro_constants::kRadToDeg,
            .elevation_m     = m_elevation_m,
            .selected_preset = -1,
            .utc_string      = utc_str,
            .lst_string      = lst_str,
            .julian_date     = m_julian_date,
            .time_scale      = m_time_scale,
            .time_paused     = (m_time_scale == 0.0),
            .bortle_scale    = m_sky_params.bortle_scale,
            .magnitude_limit = m_camera->get_magnitude_limit(),
        };
        m_side_panel.update(
            m_input->get_mouse_position(),
            m_input->was_click(),
            m_input->is_left_button_down(),
            static_cast<f32>(delta_time_sec),
            viewport.width, viewport.height,
            side_state);
    }

    // --- InfoPanel update ---
    m_info_panel.update(
        m_selection,
        m_knowledge.get(),
        m_input->get_mouse_position(),
        m_input->was_click(),
        static_cast<f32>(delta_time_sec),
        viewport.width, viewport.height);

    // --- HUD update ---
    const f32 fps = (m_delta_time > 0.0) ? static_cast<f32>(1.0 / m_delta_time) : 0.0f;

    m_hud->update(ui::HudData{
        .julian_date             = m_julian_date,
        .local_sidereal_time_rad = lst,
        .utc_hours               = 0.0,
        .altitude_deg            = pointing.alt * astro_constants::kRadToDeg,
        .azimuth_deg             = pointing.az  * astro_constants::kRadToDeg,
        .fov_deg                 = m_camera->get_fov_deg(),
        .magnitude_limit         = m_camera->get_magnitude_limit(),
        .latitude_deg            = m_observer.latitude_rad  * astro_constants::kRadToDeg,
        .longitude_deg           = m_observer.longitude_rad * astro_constants::kRadToDeg,
        .bortle_scale            = m_sky_params.bortle_scale,
        .fps                     = fps,
        .visible_stars           = m_starfield->get_visible_count(),
        .total_stars             = static_cast<u32>(m_frame_objects.size()),
        .time_scale              = m_time_scale,
        .overlay_const           = m_constellations.is_visible(),
        .overlay_grid_name       = m_coord_grid.get_type_name(),
        .overlay_dso             = m_dso_renderer.is_visible(),
        .overlay_horizon         = m_horizon.is_visible(),
        .sun_altitude_deg        = m_sun_altitude_deg,
        .atmosphere_on           = m_atmosphere_on,
    });

    // Periodic logging
    ++m_frame_counter;
    if (m_frame_counter % 60 == 0)
    {
        PLX_CORE_TRACE(
            "Universe: {} frame objects | visible stars={} | MLIM {:.1f}",
            m_frame_objects.size(),
            m_starfield->get_visible_count(),
            mag_limit);
    }
}

// =================================================================
// record_command_buffer — GPU draw order
//
// 1. Sky background (fullscreen triangle)
// 2. Starfield (instanced points, additive)
// 3. Sky overlay lines: grid → constellations → DSOs → horizon → selection indicator
// 4. Panel backgrounds (transparent filled quads)
// 5. UI border lines: toolbar separators, side panel / info panel borders
// 6. All text: HUD + toolbar + side panel + info panel (batched, single draw call)
// =================================================================

void Application::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    check_vk(vkBeginCommandBuffer(cmd, &begin_info), "vkBeginCommandBuffer");

    VkClearValue clear_color{};
    clear_color.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkExtent2D extent = m_swapchain->get_extent();

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_pipeline->get_render_pass();
    render_pass_info.framebuffer = m_pipeline->get_framebuffer(image_index);
    render_pass_info.renderArea = {{0, 0}, extent};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0.0f, 0.0f, static_cast<float>(extent.width),
                        static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 1. Sky background
    m_sky_background->draw(cmd);

    // 2. Starfield (additive blending)
    m_starfield->draw(cmd);

    // 3. Sky overlay lines: grid → constellations → DSOs → horizon + selection indicator
    m_line_renderer->render(cmd);

    // 4. Panel backgrounds (transparent filled quads — behind UI content)
    m_panel_system.render_backgrounds(cmd, extent);

    // 5. UI panel content: toolbar, side panel, info panel
    //    Each render() call submits text to the shared font queue and border
    //    lines to the line renderer; a second line_renderer→render() then
    //    flushes the UI border lines before the text draw call.
    m_line_renderer->begin_frame();
    m_toolbar.render(m_hud->get_font(), *m_line_renderer, m_panel_system, cmd, extent);
    m_side_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_info_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_line_renderer->render(cmd);   // flush UI border lines

    // 6. All text: HUD panels + toolbar + side panel + info panel (single GPU draw call)
    m_hud->render(cmd, extent);

    vkCmdEndRenderPass(cmd);

    check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
}

// =================================================================
// draw_frame — acquire image, record, submit, present
// =================================================================

void Application::draw_frame()
{
    VkDevice device = m_context->get_device();

    check_vk(vkWaitForFences(device, 1, &m_in_flight_fences[m_current_frame], VK_TRUE,
                             std::numeric_limits<uint64_t>::max()), "vkWaitForFences");

    uint32_t image_index = 0;
    VkResult acquire_result = vkAcquireNextImageKHR(device, m_swapchain->get_handle(),
        std::numeric_limits<uint64_t>::max(), m_image_available_semaphores[m_current_frame],
        VK_NULL_HANDLE, &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) { recreate_swapchain(); return; }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
    {
        PLX_CORE_CRITICAL("Failed to acquire swapchain image: {}", static_cast<int>(acquire_result));
        std::abort();
    }

    check_vk(vkResetFences(device, 1, &m_in_flight_fences[m_current_frame]), "vkResetFences");
    check_vk(vkResetCommandBuffer(m_command_buffers[m_current_frame], 0), "vkResetCommandBuffer");

    record_command_buffer(m_command_buffers[m_current_frame], image_index);

    VkSemaphore wait_semaphores[] = {m_image_available_semaphores[m_current_frame]};
    VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSemaphore signal_semaphores[] = {m_render_finished_semaphores[image_index]};

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &m_command_buffers[m_current_frame];
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    check_vk(vkQueueSubmit(m_context->get_graphics_queue(), 1, &submit_info,
                           m_in_flight_fences[m_current_frame]), "vkQueueSubmit");

    VkSwapchainKHR swapchains[] = {m_swapchain->get_handle()};
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    VkResult present_result = vkQueuePresentKHR(m_context->get_present_queue(), &present_info);

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR
        || m_framebuffer_resized)
    {
        m_framebuffer_resized = false;
        recreate_swapchain();
    }
    else if (present_result != VK_SUCCESS)
    {
        PLX_CORE_CRITICAL("Failed to present: {}", static_cast<int>(present_result));
        std::abort();
    }

    m_current_frame = (m_current_frame + 1) % kMaxFramesInFlight;
}

// =================================================================
// recreate_swapchain
// =================================================================

void Application::recreate_swapchain()
{
    uint32_t w = m_window->get_width();
    uint32_t h = m_window->get_height();
    while (w == 0 || h == 0)
    {
        m_window->poll_events();
        w = m_window->get_width();
        h = m_window->get_height();
    }

    m_context->wait_idle();

    m_swapchain->recreate(w, h);

    m_pipeline = std::make_unique<vulkan::Pipeline>(*m_context, *m_swapchain,
        std::filesystem::path{PLX_SHADER_DIR});

    const auto extent = m_swapchain->get_extent();
    m_sky_background->set_extent(extent);

    for (auto sem : m_render_finished_semaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_context->get_device(), sem, nullptr);
        }
    }
    m_render_finished_semaphores.resize(m_swapchain->get_image_count());
    for (auto& sem : m_render_finished_semaphores)
    {
        VkSemaphoreCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check_vk(vkCreateSemaphore(m_context->get_device(), &ci, nullptr, &sem),
                 "vkCreateSemaphore (recreate)");
    }

    PLX_CORE_INFO("Swapchain recreated: {}x{}", extent.width, extent.height);
}

void Application::create_command_pool()
{
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = m_context->get_graphics_queue_family();
    check_vk(vkCreateCommandPool(m_context->get_device(), &pool_info, nullptr, &m_command_pool),
             "vkCreateCommandPool");
}

void Application::create_command_buffers()
{
    m_command_buffers.resize(kMaxFramesInFlight);
    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = kMaxFramesInFlight;
    check_vk(vkAllocateCommandBuffers(m_context->get_device(), &alloc_info, m_command_buffers.data()),
             "vkAllocateCommandBuffers");
}

void Application::create_sync_objects()
{
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        check_vk(vkCreateSemaphore(m_context->get_device(), &sem_info, nullptr,
                                   &m_image_available_semaphores[i]),
                 "vkCreateSemaphore (image available)");
        check_vk(vkCreateFence(m_context->get_device(), &fence_info, nullptr,
                               &m_in_flight_fences[i]),
                 "vkCreateFence");
    }

    m_render_finished_semaphores.resize(m_swapchain->get_image_count());
    for (auto& sem : m_render_finished_semaphores)
    {
        check_vk(vkCreateSemaphore(m_context->get_device(), &sem_info, nullptr, &sem),
                 "vkCreateSemaphore (render finished)");
    }
}

void Application::destroy_sync_objects()
{
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (m_image_available_semaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_context->get_device(), m_image_available_semaphores[i], nullptr);
        }
        if (m_in_flight_fences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(m_context->get_device(), m_in_flight_fences[i], nullptr);
        }
    }

    for (auto sem : m_render_finished_semaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_context->get_device(), sem, nullptr);
        }
    }
    m_render_finished_semaphores.clear();
}

} // namespace parallax::core