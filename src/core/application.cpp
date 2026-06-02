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

#include "analysis/mock_analyzer.hpp"
#include "core/user_data_path.hpp"
#include "instruments/mock_instrument.hpp"
#include "knowledge/knowledge_database.hpp"
#include "knowledge/knowledge_level.hpp"
#include "observation/data_archive.hpp"
#include "observation/session_scheduler.hpp"
#include "rendering/render_style.hpp"                    // ← SPRINT 08 Task 8.9

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#include <string>
#include <unordered_set>

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

constexpr float kDefaultSnrRatePerHour = 5.0f;
constexpr std::size_t kCompletedSessionsDisplayLimit = 10;

std::string knowledge_level_to_text(parallax::knowledge::KnowledgeLevel level)
{
    using parallax::knowledge::KnowledgeLevel;

    switch (level)
    {
        case KnowledgeLevel::Detected:      return "L1";
        case KnowledgeLevel::Classified:    return "L2";
        case KnowledgeLevel::Characterized: return "L3";
        case KnowledgeLevel::Detailed:      return "L4";
        case KnowledgeLevel::Resolved:      return "L5";
        case KnowledgeLevel::FullyMapped:   return "L6";
        default:                            return "L0";
    }
}

std::string format_object_label(const parallax::universe::Universe& universe, parallax::u64 object_id)
{
    if (object_id == 0)
    {
        return "[Survey]";
    }

    const std::string_view known_name = universe.get_name(object_id);
    if (!known_name.empty())
    {
        return std::string{known_name};
    }

    if (const auto object = universe.query_object(object_id); object.has_value())
    {
        const parallax::u64 source_id = parallax::universe::decode_source_id(object_id);
        switch (object->type)
        {
            case parallax::universe::ObjectType::Star:
                return std::format("HIP {}", source_id);
            case parallax::universe::ObjectType::DeepSkyObject:
            case parallax::universe::ObjectType::Galaxy:
                return std::format("M{}", source_id);
            case parallax::universe::ObjectType::SolarSystemBody:
                return std::format("Body {}", source_id);
            case parallax::universe::ObjectType::ProceduralStar:
            case parallax::universe::ObjectType::ProceduralDso:
                return std::format("PRC {:016X}", object_id);
            default:
                return std::format("ID {}", object_id);
        }
    }

    return std::format("ID {}", object_id);
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

    // 6. Line renderer for UI border passes
    m_line_renderer = std::make_unique<rendering::LineRenderer>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 7. HUD overlay
    m_hud = std::make_unique<ui::Hud>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 7b. PanelSystem — batched panel backgrounds           ← SPRINT 05 Task 5.1
    m_panel_system.init(*m_context, m_pipeline->get_render_pass(), shader_dir);

    // 7c. Toolbar                                           ← SPRINT 05 Task 5.3
    {
        ui::ToolbarCallbacks cb;
        cb.toggle_constellations = [this]() { m_planetarium_tab->toggle_constellations(); };
        cb.toggle_stars          = [this]() { /* TODO: add Starfield visibility toggle */ };
        cb.toggle_dso            = [this]() { m_planetarium_tab->toggle_dso(); };
        cb.cycle_grid            = [this]() { m_planetarium_tab->cycle_grid(); };
        cb.toggle_horizon        = [this]() { m_planetarium_tab->toggle_horizon(); };
        cb.toggle_atmosphere     = [this]() { toggle_atmosphere(); };  // ← SPRINT 06 Task 6.7
        cb.toggle_observe_panel  = [this]()
        {
            m_show_instrument_panel = !m_show_instrument_panel;
            m_instrument_panel.set_visible(m_show_instrument_panel);
        };
        cb.toggle_sessions_panel = [this]()
        {
            m_show_sessions_panel = !m_show_sessions_panel;
            m_sessions_panel.set_visible(m_show_sessions_panel);
        };
        cb.toggle_data_panel     = [this]()
        {
            m_show_data_archive_panel = !m_show_data_archive_panel;
            m_data_archive_panel.set_visible(m_show_data_archive_panel);
        };
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
        cb.set_fov = [this](f64 fov_deg) { m_planetarium_tab->set_fov(fov_deg); };
        m_toolbar.init(cb);
    }

    // 7d. SidePanel                                         ← SPRINT 05 Task 5.4
    {
        ui::SidePanelCallbacks cb;
        cb.set_location = [this](f64 lat_deg, f64 lon_deg, f64 elev_m, f32 bortle)
        {
            m_observer.latitude_rad  = glm::radians(lat_deg);
            m_observer.longitude_rad = glm::radians(lon_deg);
            m_elevation_m            = elev_m;
            m_planetarium_tab->set_bortle_scale(bortle);
            PLX_CORE_INFO("Observer location set: {:.2f}N {:.2f}E {:.0f}m Bortle {}",
                          lat_deg, lon_deg, elev_m, static_cast<int>(bortle));
        };
        cb.set_bortle = [this](f32 bortle)
        {
            m_planetarium_tab->set_bortle_scale(bortle);
            PLX_CORE_INFO("Bortle scale: {}", static_cast<int>(bortle));
        };
        cb.set_magnitude_limit = [this](f32 mag)
        {
            m_planetarium_tab->set_magnitude_limit(mag);
            PLX_CORE_INFO("Magnitude limit: {:.1f}", mag);
        };
        cb.set_time_scale = [this](f64 scale)
        {
            m_time_scale = scale;
            PLX_CORE_INFO("Time scale: x{}", scale);
        };
        m_side_panel.init(cb);
    }

    // 7e. InfoPanel                                         ← SPRINT 05 Task 5.5
    {
        ui::InfoPanelCallbacks cb;
        cb.track = [this]()
        {
            m_planetarium_tab->toggle_tracking();
            PLX_CORE_INFO("Tracking {}",
                          m_planetarium_tab->get_selection().is_tracking() ? "enabled" : "disabled");
        };
        cb.goto_object = [this]() { /* TODO: future telescope slew command */ };
        cb.observe_this = [this](u64 target_id) { request_observe(target_id); };
        m_info_panel.init(cb);
    }

    // 7f. Observation workflow panels                        ← SPRINT 08 Task 8.10
    {
        ui::InstrumentPanelCallbacks observe_cb;
        observe_cb.schedule = [this](const observation::SessionParameters& params)
        {
            if (m_scheduler)
            {
                const u64 id = m_scheduler->schedule(params);
                PLX_CORE_INFO("Scheduled observation session {}", id);
            }
        };
        m_instrument_panel.init(observe_cb);
        m_instrument_panel.set_visible(false);

        ui::SessionsPanelCallbacks sessions_cb;
        sessions_cb.abort_session = [this](u64 session_id)
        {
            if (m_scheduler)
            {
                m_scheduler->abort(session_id);
            }
        };
        m_sessions_panel.init(sessions_cb);
        m_sessions_panel.set_visible(false);

        m_data_archive_panel.init();
        m_data_archive_panel.set_visible(false);
    }

    // =================================================================
    // 8. Universe facade — replaces all direct catalog loading.
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

    // 8b. Sprint 08 integration modules.
    m_knowledge = std::make_unique<knowledge::KnowledgeDatabase>();

    const std::filesystem::path save_dir = user_data_save_dir();
    const std::filesystem::path knowledge_path = save_dir / "knowledge.json";
    if (std::filesystem::exists(knowledge_path) && m_knowledge->load(knowledge_path))
    {
        PLX_CORE_INFO("Loaded knowledge save: {}", knowledge_path.string());
    }
    else
    {
        m_knowledge->initialize_from_historical_catalogs(*m_universe);
        PLX_CORE_INFO("Initialized knowledge from historical catalogs (will save to: {})",
                      knowledge_path.string());
    }

    m_scheduler = std::make_unique<observation::SessionScheduler>();
    m_archive = std::make_unique<observation::DataArchive>();
    const std::filesystem::path archive_path = save_dir / "archive.json";
    if (std::filesystem::exists(archive_path) && m_archive->load(archive_path))
    {
        PLX_CORE_INFO("Loaded data archive save: {}", archive_path.string());
    }
    else
    {
        PLX_CORE_INFO("Initialized empty data archive: {}", archive_path.string());
    }

    m_mock_instrument = std::make_unique<instruments::MockInstrument>(1, "Magic Instrument");
    m_analyzer = std::make_unique<analysis::MockAnalyzer>();

    // 9. Observer: La Palma, Canary Islands
    m_observer = astro::ObserverLocation{
        .latitude_rad  = glm::radians(28.76),
        .longitude_rad = glm::radians(-17.89),
    };

    // 10. Simulation time
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

    // 11. Planetarium tab
    m_planetarium_tab = std::make_unique<ui::tabs::PlanetariumTab>(
        *m_context,
        *m_swapchain,
        m_pipeline->get_render_pass(),
        shader_dir,
        *m_universe,
        *m_knowledge,
        m_hud->get_font(),
        m_julian_date,
        m_observer);
    m_planetarium_tab->set_bortle_scale(4.0f);

    // 12-14. Command pool, sync, frame time
    create_command_pool();
    create_command_buffers();
    create_sync_objects();
    m_last_frame_time = std::chrono::steady_clock::now();

    // Log initial overlay states
    PLX_CORE_INFO("Overlays: CONST={} GRID={} DSO={} HORIZ={}",
                  m_planetarium_tab->constellations_visible() ? "ON" : "OFF",
                  m_planetarium_tab->grid_type_name(),
                  m_planetarium_tab->dso_visible() ? "ON" : "OFF",
                  m_planetarium_tab->horizon_visible() ? "ON" : "OFF");

    PLX_CORE_INFO("Application initialized — skychart mode, MLIM {:.1f}",
                  m_planetarium_tab->get_magnitude_limit());
}

// =================================================================
// Shutdown
// =================================================================

void Application::shutdown()
{
    if (m_knowledge)
    {
        const std::filesystem::path save_dir = user_data_save_dir();
        const std::filesystem::path knowledge_path = save_dir / "knowledge.json";
        if (!m_knowledge->save(knowledge_path))
        {
            PLX_CORE_WARN("Failed to save knowledge database: {}", knowledge_path.string());
        }
    }

    if (m_archive)
    {
        const std::filesystem::path save_dir = user_data_save_dir();
        const std::filesystem::path archive_path = save_dir / "archive.json";
        if (!m_archive->save(archive_path))
        {
            PLX_CORE_WARN("Failed to save data archive: {}", archive_path.string());
        }
    }

    m_analyzer.reset();
    m_mock_instrument.reset();
    m_archive.reset();
    m_scheduler.reset();
    m_knowledge.reset();

    if (!m_context) return;

    m_context->wait_idle();
    destroy_sync_objects();

    if (m_command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_context->get_device(), m_command_pool, nullptr);
        m_command_pool = VK_NULL_HANDLE;
    }

    m_panel_system.destroy();   // ← SPRINT 05 Task 5.1 (destroy before HUD)
    m_planetarium_tab.reset();
    m_hud.reset();
    m_line_renderer.reset();
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

bool Application::is_atmosphere_on() const
{
    return m_planetarium_tab->is_atmosphere_on();
}

void Application::toggle_atmosphere()
{
    m_planetarium_tab->toggle_atmosphere();
}

void Application::set_atmosphere(bool on)
{
    m_planetarium_tab->set_atmosphere(on);
}

void Application::request_observe(u64 target_id)
{
    if (target_id == 0)
    {
        return;
    }

    m_show_instrument_panel = true;
    m_instrument_panel.open_for_selected_object(target_id);
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
    const VkExtent2D extent = m_swapchain->get_extent();
    const ui::shell::ViewportRect planetarium_viewport{
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height
    };

    m_planetarium_tab->set_viewport(planetarium_viewport);

    // =================================================================
    // Step 1: Determine if mouse is over any UI panel
    // =================================================================
    const bool mouse_over_ui = m_panel_system.is_mouse_over_ui(mouse_pos)
                            || m_toolbar.is_mouse_over(mouse_pos)
                            || m_side_panel.is_mouse_over(mouse_pos)
                            || m_info_panel.is_mouse_over(mouse_pos)
                            || m_instrument_panel.is_mouse_over(mouse_pos)
                            || m_sessions_panel.is_mouse_over(mouse_pos)
                            || m_data_archive_panel.is_mouse_over(mouse_pos);

    // =================================================================
    // Step 2: Route mouse input based on priority
    // =================================================================
    if (mouse_over_ui)
    {
        m_panel_system.process_input(*m_input, mouse_pos);
        m_input->set_cursor(CursorStyle::Hand);
    }
    else
    {
        ui::shell::InputEvent event{};
        event.inside_viewport = planetarium_viewport.contains(
            static_cast<i32>(mouse_pos.x),
            static_cast<i32>(mouse_pos.y));
        event.scroll_delta = m_input->get_scroll_delta();
        event.is_dragging = m_input->is_mouse_dragging();
        event.drag_delta = m_input->get_mouse_drag_delta();
        event.mouse_delta = event.drag_delta;

        if (event.inside_viewport)
        {
            event.mouse_pos = {
                mouse_pos.x - static_cast<f32>(planetarium_viewport.x),
                mouse_pos.y - static_cast<f32>(planetarium_viewport.y)
            };
        }

        if (m_input->was_click())
        {
            const Vec2f click_pos = m_input->get_click_position();
            if (planetarium_viewport.contains(static_cast<i32>(click_pos.x), static_cast<i32>(click_pos.y)))
            {
                event.was_click = true;
                event.click_button = ui::shell::MouseButton::Left;
                event.click_pos = {
                    click_pos.x - static_cast<f32>(planetarium_viewport.x),
                    click_pos.y - static_cast<f32>(planetarium_viewport.y)
                };
            }
        }

        m_planetarium_tab->on_input(event, planetarium_viewport);

        if (event.inside_viewport)
        {
            m_input->set_cursor(event.is_dragging ? CursorStyle::SizeAll : CursorStyle::Crosshair);
        }
        else
        {
            m_input->set_cursor(CursorStyle::Arrow);
        }
    }

    // =================================================================
    // Keyboard bindings (always active regardless of mouse state)
    // =================================================================

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
        f32 bortle = m_planetarium_tab->get_bortle_scale() + 1.0f;
        if (bortle > 9.0f) bortle = 1.0f;
        m_planetarium_tab->set_bortle_scale(bortle);
        PLX_CORE_INFO("Bortle scale: {}", static_cast<int>(bortle));
    }

    const bool had_selection = m_planetarium_tab->has_selection();
    m_planetarium_tab->handle_keyboard(*m_input);

    if (m_input->is_key_pressed(SDL_SCANCODE_ESCAPE) && !had_selection)
    {
        m_window->request_close();
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

    if (m_scheduler && m_universe)
    {
        m_scheduler->update(m_julian_date, delta_time_sec, *m_universe);

        std::vector<std::uint64_t> completed_ids;
        for (const auto* session : m_scheduler->get_completed())
        {
            completed_ids.push_back(session->id());
        }

        for (const std::uint64_t session_id : completed_ids)
        {
            auto maybe = m_scheduler->harvest(session_id);
            if (!maybe.has_value())
            {
                continue;
            }

            observation::DataRecord data = std::move(*maybe);
            // DataArchive is keyed by DataRecord::id; bind it to session_id for
            // mock-session records so each harvested session persists as a unique row.
            data.id = data.session_id;

            if (m_analyzer && m_knowledge)
            {
                const auto updates = m_analyzer->analyze(data, *m_universe);
                std::unordered_set<u64> detection_targets;
                for (const auto& update : updates)
                {
                    if (update.object_id != 0 && !detection_targets.contains(update.object_id))
                    {
                        m_knowledge->add_detection(update.object_id, session_id);
                        detection_targets.insert(update.object_id);
                    }

                    m_knowledge->record_measurement(
                        update.object_id,
                        update.property_name,
                        update.value,
                        update.uncertainty,
                        update.snr,
                        session_id);
                }
            }

            if (m_archive)
            {
                m_archive->add(std::make_unique<observation::DataRecord>(std::move(data)));
            }
        }
    }

    const VkExtent2D viewport = m_swapchain->get_extent();
    m_planetarium_tab->set_viewport({
        .x = 0,
        .y = 0,
        .width = viewport.width,
        .height = viewport.height
    });
    m_planetarium_tab->update(delta_time_sec);

    const f64 lst = astro::TimeSystem::lmst(m_julian_date, m_observer.longitude_rad);
    const auto& camera = m_planetarium_tab->get_camera();
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const auto camera_eq = astro::Coordinates::horizontal_to_equatorial(pointing, m_observer, lst);

    // --- Toolbar update ---
    {
        const ui::ToolbarState toolbar_state{
            .constellations_visible = m_planetarium_tab->constellations_visible(),
            .stars_visible          = true,
            .dso_visible            = m_planetarium_tab->dso_visible(),
            .grid_visible           = m_planetarium_tab->grid_type() != overlay::GridType::None,
            .horizon_visible        = m_planetarium_tab->horizon_visible(),
            .atmosphere_on          = m_planetarium_tab->is_atmosphere_on(),
            .observe_panel_visible  = m_show_instrument_panel,
            .sessions_panel_visible = m_show_sessions_panel,
            .data_panel_visible     = m_show_data_archive_panel,
            .time_scale             = m_time_scale,
            .time_paused            = (m_time_scale == 0.0),
            .fov_deg                = m_planetarium_tab->get_fov_deg(),
            .magnitude_limit        = m_planetarium_tab->get_magnitude_limit(),
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
            .bortle_scale    = m_planetarium_tab->get_bortle_scale(),
            .magnitude_limit = m_planetarium_tab->get_magnitude_limit(),
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
        m_planetarium_tab->get_selection(),
        m_knowledge.get(),
        m_input->get_mouse_position(),
        m_input->was_click(),
        static_cast<f32>(delta_time_sec),
        viewport.width, viewport.height);

    // --- Instrument panel update ---
    {
        ui::InstrumentPanelState observe_state;
        observe_state.has_selection = m_planetarium_tab->has_selection();
        if (observe_state.has_selection)
        {
            observe_state.selected_object_id = m_planetarium_tab->get_selection().get_selection().celestial_obj.id;
            observe_state.selected_object_name =
                format_object_label(*m_universe, observe_state.selected_object_id);
        }
        observe_state.center_ra_rad = camera_eq.ra;
        observe_state.center_dec_rad = camera_eq.dec;
        observe_state.fov_rad = fov_rad;
        observe_state.current_julian_date = m_julian_date;

        m_instrument_panel.update(
            observe_state,
            m_input->get_mouse_position(),
            m_input->was_click(),
            m_input->is_left_button_down(),
            static_cast<f32>(delta_time_sec),
            viewport.width, viewport.height);
        m_show_instrument_panel = m_instrument_panel.is_visible();
    }

    // --- Sessions panel update ---
    {
        std::vector<ui::SessionsPanelEntry> active_entries;
        std::vector<ui::SessionsPanelCompletedEntry> completed_entries;

        if (m_scheduler)
        {
            const auto active = m_scheduler->get_active();
            active_entries.reserve(active.size());
            for (const auto* session : active)
            {
                const auto& params = session->parameters();
                const auto& progress = session->progress();
                active_entries.push_back(ui::SessionsPanelEntry{
                    .session_id = session->id(),
                    .target_name = format_object_label(*m_universe, params.target_object_id),
                    .technique = params.technique,
                    .completion_fraction = static_cast<f32>(progress.completion_fraction),
                    .elapsed_hours = static_cast<f32>(progress.elapsed_hours),
                    .accumulated_snr = static_cast<f32>(progress.accumulated_snr),
                    .expected_snr = static_cast<f32>(
                        params.planned_duration_hours
                        * static_cast<double>(m_mock_instrument
                                                   ? m_mock_instrument->get_snr_rate_per_hour()
                                                   : kDefaultSnrRatePerHour)),
                });
            }

        }

        if (m_archive)
        {
            auto completed = m_archive->get_all();
            std::sort(completed.begin(), completed.end(),
                      [](const observation::DataRecord* lhs, const observation::DataRecord* rhs)
                      {
                          return lhs->session_id > rhs->session_id;
                      });

            const std::size_t max_count = std::min<std::size_t>(kCompletedSessionsDisplayLimit, completed.size());
            completed_entries.reserve(max_count);
            for (std::size_t i = 0; i < max_count; ++i)
            {
                const auto* record = completed[i];
                std::string level_text = "--";
                if (m_knowledge && record->target_object_id != 0)
                {
                    level_text = knowledge_level_to_text(m_knowledge->get_level(record->target_object_id));
                }

                completed_entries.push_back(ui::SessionsPanelCompletedEntry{
                    .session_id = record->session_id,
                    .target_name = format_object_label(*m_universe, record->target_object_id),
                    .technique = record->technique,
                    .final_snr = static_cast<f32>(record->achieved_snr),
                    .level_achieved = std::move(level_text),
                });
            }
        }

        m_sessions_panel.update(
            std::move(active_entries),
            std::move(completed_entries),
            m_input->get_mouse_position(),
            m_input->was_click(),
            static_cast<f32>(delta_time_sec),
            viewport.width, viewport.height);
    }

    // --- Data archive panel update ---
    {
        std::vector<ui::DataArchivePanelRow> rows;
        if (m_archive)
        {
            auto records = m_archive->get_all();
            std::sort(records.begin(), records.end(),
                      [](const observation::DataRecord* lhs, const observation::DataRecord* rhs)
                      {
                          return lhs->observation_jd > rhs->observation_jd;
                      });

            rows.reserve(records.size());
            for (std::size_t i = 0; i < records.size(); ++i)
            {
                rows.push_back(ui::DataArchivePanelRow{
                    .index = i,
                    .record = records[i],
                    .target_name = format_object_label(*m_universe, records[i]->target_object_id),
                });
            }
        }

        m_data_archive_panel.update(
            std::move(rows),
            m_input->get_mouse_position(),
            m_input->was_click(),
            viewport.width, viewport.height);
    }

    // --- HUD update ---
    const f32 fps = (m_delta_time > 0.0) ? static_cast<f32>(1.0 / m_delta_time) : 0.0f;

    m_hud->update(ui::HudData{
        .julian_date             = m_julian_date,
        .local_sidereal_time_rad = lst,
        .utc_hours               = 0.0,
        .altitude_deg            = pointing.alt * astro_constants::kRadToDeg,
        .azimuth_deg             = pointing.az  * astro_constants::kRadToDeg,
        .fov_deg                 = m_planetarium_tab->get_fov_deg(),
        .magnitude_limit         = m_planetarium_tab->get_magnitude_limit(),
        .latitude_deg            = m_observer.latitude_rad  * astro_constants::kRadToDeg,
        .longitude_deg           = m_observer.longitude_rad * astro_constants::kRadToDeg,
        .bortle_scale            = m_planetarium_tab->get_bortle_scale(),
        .fps                     = fps,
        .visible_stars           = m_planetarium_tab->visible_star_count(),
        .total_stars             = static_cast<u32>(m_planetarium_tab->get_frame_objects().size()),
        .time_scale              = m_time_scale,
        .overlay_const           = m_planetarium_tab->constellations_visible(),
        .overlay_grid_name       = m_planetarium_tab->grid_type_name(),
        .overlay_dso             = m_planetarium_tab->dso_visible(),
        .overlay_horizon         = m_planetarium_tab->horizon_visible(),
        .sun_altitude_deg        = m_planetarium_tab->get_sun_altitude_deg(),
        .atmosphere_on           = m_planetarium_tab->is_atmosphere_on(),
    });

    // Periodic logging
    ++m_frame_counter;
    if (m_frame_counter % 60 == 0)
    {
        PLX_CORE_TRACE(
            "Universe: {} frame objects | visible stars={} | MLIM {:.1f}",
            m_planetarium_tab->get_frame_objects().size(),
            m_planetarium_tab->visible_star_count(),
            m_planetarium_tab->get_magnitude_limit());
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

    const ui::shell::ViewportRect planetarium_viewport{
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height
    };

    // 1-3. Planetarium tab
    m_planetarium_tab->render(cmd, planetarium_viewport);

    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 4. Panel backgrounds (transparent filled quads — behind UI content)
    m_panel_system.render_backgrounds(cmd, extent);

    // 5. UI panel content
    //    Each render() call submits text to the shared font queue and border
    //    lines to the line renderer; a second line_renderer→render() then
    //    flushes the UI border lines before the text draw call.
    m_line_renderer->begin_frame();
    m_toolbar.render(m_hud->get_font(), *m_line_renderer, m_panel_system, cmd, extent);
    m_side_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_info_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_instrument_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_sessions_panel.render(m_hud->get_font(), *m_line_renderer, extent);
    m_data_archive_panel.render(m_hud->get_font(), *m_line_renderer, extent);
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
    m_planetarium_tab->set_viewport({
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height
    });

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
