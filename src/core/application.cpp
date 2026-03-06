/// @file application.cpp
/// @brief Application implementation — skychart mode.
///
/// Frame loop: input → time → sky → prefilter → starfield → overlays → HUD → render → present.
/// NO atmospheric effects on star rendering.
///
/// SPRINT 04 Task 4.7: Full overlay integration.
/// Render order:
///   1. Sky background
///   2. Coordinate grid (behind stars)
///   3. Starfield (additive)
///   4. Constellation lines + labels (over stars)
///   5. DSO icons + labels (over stars)
///   6. Horizon line + cardinal markers (over everything except HUD)
///   7. HUD (always on top)

#include "core/application.hpp"

#include "catalog/catalog_loader.hpp"
#include "catalog/dso_loader.hpp"

#include <glm/trigonometric.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
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
    m_starfield = std::make_unique<rendering::Starfield>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 7b. Line renderer for overlays (constellations, grids, horizon)  ← SPRINT 04 Task 4.1
    m_line_renderer = std::make_unique<rendering::LineRenderer>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 8. Camera
    m_camera = std::make_unique<rendering::Camera>();

    // 9. HUD overlay
    m_hud = std::make_unique<ui::Hud>(
        *m_context, m_pipeline->get_render_pass(), shader_dir);

    // 10. Load star catalog
    const std::filesystem::path hipparcos_path{"data/catalogs/hipparcos.csv"};
    const std::filesystem::path bright_path{"data/catalogs/bright_stars.csv"};

    auto loaded_stars = catalog::CatalogLoader::load_hipparcos_csv(hipparcos_path);
    if (loaded_stars.has_value())
    {
        m_stars = std::move(loaded_stars.value());
        PLX_CORE_INFO("Hipparcos catalog loaded: {} stars from {}",
                      m_stars.size(), hipparcos_path.string());
    }
    else
    {
        PLX_CORE_WARN("Hipparcos catalog not found at {}. Trying bright star fallback...",
                      hipparcos_path.string());
        auto fallback = catalog::CatalogLoader::load_bright_star_csv(bright_path);
        if (fallback.has_value())
        {
            m_stars = std::move(fallback.value());
            PLX_CORE_INFO("Bright star catalog loaded: {} stars from {}",
                          m_stars.size(), bright_path.string());
        }
        else
        {
            PLX_CORE_WARN("No catalog found. Rendering will show no stars.");
        }
    }

    // 10b. Load constellation overlay                                   ← SPRINT 04 Task 4.2
    {
        const std::filesystem::path const_lines{"data/catalogs/constellation_lines.csv"};
        const std::filesystem::path const_names{"data/catalogs/constellation_names.csv"};
        if (m_constellations.load(const_lines, const_names))
        {
            m_constellations.resolve_stars(m_stars);
        }
    }

    // 10c. Load Messier DSO catalog                                     ← SPRINT 04 Task 4.5
    {
        const std::filesystem::path messier_path{"data/catalogs/messier.csv"};
        auto loaded_dsos = catalog::DsoLoader::load_messier_csv(messier_path);
        if (loaded_dsos.has_value())
        {
            m_dsos = std::move(loaded_dsos.value());
            PLX_CORE_INFO("Messier catalog loaded: {} objects from {}",
                          m_dsos.size(), messier_path.string());
        }
        else
        {
            PLX_CORE_WARN("Messier catalog not found at {}", messier_path.string());
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

    // 13. Sky parameters (visual context only — NOT applied to stars)
    m_sky_params = rendering::SkyParams{
        .bortle_scale = 4.0f,
        .sun_altitude_deg = -30.0f,
        .moon_altitude_deg = -90.0f,
        .moon_phase = 0.0f,
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
// Input processing — all key bindings                   ← SPRINT 04 Task 4.7
// =================================================================

void Application::process_input()
{
    // Mouse drag → Camera pan
    if (m_input->is_mouse_dragging())
    {
        const auto drag = m_input->get_mouse_drag_delta();
        const f64 fov = m_camera->get_fov_rad();
        const f64 sensitivity = fov / static_cast<f64>(m_window->get_width());

        const f64 delta_az  = -static_cast<f64>(drag.x) * sensitivity;
        const f64 delta_alt = -static_cast<f64>(drag.y) * sensitivity;
        m_camera->pan(delta_az, delta_alt);
    }

    // Scroll → Camera zoom
    const f32 scroll = m_input->get_scroll_delta();
    if (scroll != 0.0f)
    {
        const f64 zoom_factor = 1.0 - static_cast<f64>(scroll) * 0.1;
        m_camera->zoom(zoom_factor);
    }

    // =================================================================
    // Magnitude limit: [ and ] keys (or PageUp / PageDown)
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

    // Time scale controls
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

    // =================================================================
    // UI controls
    // =================================================================

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

    // =================================================================
    // Overlay toggles                                   ← SPRINT 04 Task 4.7
    // =================================================================

    // C — Constellation lines + labels
    if (m_input->is_key_pressed(SDL_SCANCODE_C))
    {
        m_constellations.toggle_visible();
        PLX_CORE_INFO("Constellations {}",
                      m_constellations.is_visible() ? "shown" : "hidden");
    }

    // G — Cycle coordinate grid (None → Eq → AltAz → Both → None)
    if (m_input->is_key_pressed(SDL_SCANCODE_G))
    {
        m_coord_grid.cycle_type();
        PLX_CORE_INFO("Coordinate grid: {}", m_coord_grid.get_type_name());
    }

    // D — Toggle deep sky objects (Messier)
    if (m_input->is_key_pressed(SDL_SCANCODE_D))
    {
        m_dso_renderer.toggle_visible();
        PLX_CORE_INFO("DSOs {}", m_dso_renderer.is_visible() ? "shown" : "hidden");
    }

    // O — Toggle horizon overlay + cardinal markers
    if (m_input->is_key_pressed(SDL_SCANCODE_O))
    {
        m_horizon.toggle_visible();
        PLX_CORE_INFO("Horizon {}", m_horizon.is_visible() ? "shown" : "hidden");
    }

    // =================================================================
    // Camera reset + quit
    // =================================================================

    if (m_input->is_key_pressed(SDL_SCANCODE_R))
    {
        m_camera->reset();
        PLX_CORE_INFO("Camera reset (MLIM {:.1f})", m_camera->get_magnitude_limit());
    }

    if (m_input->is_key_pressed(SDL_SCANCODE_ESCAPE))
    {
        m_window->request_close();
    }
}

// =================================================================
// Simulation update — skychart mode (no atmosphere on stars)
//
// Render order per sprint_04.md Task 4.7:
//   1. Sky background   (updated first, drawn by record_command_buffer)
//   2. Coordinate grid  (behind stars — submitted to line renderer)
//   3. Starfield        (additive)
//   4. Constellations   (over stars — lines + labels)
//   5. DSO icons+labels (over stars)
//   6. Horizon+cardinals(over everything except HUD)
//   7. HUD              (always on top)
//
// All overlay geometry is submitted to the single m_line_renderer.
// Labels are submitted to the BitmapFont inside m_hud.
// The actual GPU draw order is:
//   sky_bg → starfield → line_renderer (all overlays) → HUD
// The submit order below controls Z-layering within the line batch.
// =================================================================

void Application::update_simulation(f64 delta_time_sec)
{
    // Advance Julian Date
    m_julian_date += (delta_time_sec * m_time_scale) / 86400.0;

    // Compute Local Sidereal Time
    const f64 lst = astro::TimeSystem::lmst(m_julian_date, m_observer.longitude_rad);

    // Update sky background (visual context only)
    m_sky_background->update_params(m_sky_params, *m_camera);

    // Clear line renderer for this frame                               ← SPRINT 04 Task 4.1
    m_line_renderer->begin_frame();

    const VkExtent2D viewport = m_swapchain->get_extent();

    // --- Step 2: Coordinate grid (behind stars visually, but all lines
    //     are drawn in one batch after starfield in the render pass).
    //     Submitted first so grid lines are "underneath" overlay lines. ---
    m_coord_grid.update(*m_camera, m_observer, lst,
                        *m_line_renderer, m_hud->get_font(),
                        viewport);

    // Visibility prefilter
    catalog::PrefilterStats prefilter_stats{}
    const auto candidates = catalog::VisibilityFilter::filter(
        m_stars, m_observer, lst, m_camera->get_magnitude_limit(), &prefilter_stats);

    // --- Step 3: Starfield update — NO atmosphere parameter ---
    m_starfield->update(m_stars, candidates, m_observer, lst, *m_camera);

    // --- Step 4: Constellation lines + labels (over stars) ---
    m_constellations.update(*m_camera, m_observer, lst,
                            *m_line_renderer, m_hud->get_font(),
                            viewport);

    // --- Step 5: DSO icons + labels (over stars) ---
    m_dso_renderer.update(*m_camera, m_observer, lst,
                          m_dsos, *m_line_renderer, m_hud->get_font(),
                          viewport);

    // --- Step 6: Horizon line + cardinal markers (over everything except HUD) ---
    m_horizon.update(*m_camera, *m_line_renderer, m_hud->get_font(),
                     viewport);

    // --- Step 7: Update HUD (drawn last in render pass) ---
    const auto pointing = m_camera->get_pointing();
    const f32 fps = (m_delta_time > 0.0) ? static_cast<f32>(1.0 / m_delta_time) : 0.0f;

    m_hud->update(ui::HudData{
        .julian_date             = m_julian_date,
        .local_sidereal_time_rad = lst,
        .utc_hours               = 0.0,
        .altitude_deg            = pointing.alt * astro_constants::kRadToDeg,
        .azimuth_deg             = pointing.az * astro_constants::kRadToDeg,
        .fov_deg                 = m_camera->get_fov_deg(),
        .magnitude_limit         = m_camera->get_magnitude_limit(),
        .latitude_deg            = m_observer.latitude_rad * astro_constants::kRadToDeg,
        .longitude_deg           = m_observer.longitude_rad * astro_constants::kRadToDeg,
        .bortle_scale            = m_sky_params.bortle_scale,
        .fps                     = fps,
        .visible_stars           = m_starfield->get_visible_count(),
        .total_stars             = static_cast<u32>(m_stars.size()),
        .time_scale              = m_time_scale,
        .overlay_const           = m_constellations.is_visible(),
        .overlay_grid_name       = m_coord_grid.get_type_name(),
        .overlay_dso             = m_dso_renderer.is_visible(),
        .overlay_horizon         = m_horizon.is_visible(),
    });

    // Periodic logging
    ++m_frame_counter;
    if (m_frame_counter % 60 == 0)
    {
        PLX_CORE_TRACE(
            "Stars: {} total | {} passed | {} visible | MLIM {:.1f}",
            prefilter_stats.total,
            prefilter_stats.passed,
            m_starfield->get_visible_count(),
            m_camera->get_magnitude_limit());
    }
}

// =================================================================
// record_command_buffer — GPU draw order
//
// 1. Sky background (fullscreen triangle)
// 2. Starfield (instanced points, additive)
// 3. Line renderer (all overlay geometry: grid, constellations,
//                    DSO icons, horizon — in submit order)
// 4. HUD (bitmap font quads, alpha blended — always on top)
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

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width),
                        static_cast<float>(extent.height), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 1. Sky background
    m_sky_background->draw(cmd);

    // 2. Starfield (additive blending)
    m_starfield->draw(cmd);

    // 3. All overlay lines: grid → constellations → DSOs → horizon
    m_line_renderer->render(cmd);

    // 4. HUD (always on top)
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

void Application::recreate_swapchain()
{
    m_context->wait_idle();
    m_swapchain = std::make_unique<vulkan::Swapchain>(
        *m_context, m_window->get_width(), m_window->get_height());
    m_pipeline = std::make_unique<vulkan::Pipeline>(*m_context, *m_swapchain,
        std::filesystem::path{PLX_SHADER_DIR});

    const auto extent = m_swapchain->get_extent();
    m_sky_background->set_extent(extent);

    // Recreate per-image render-finished semaphores
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
