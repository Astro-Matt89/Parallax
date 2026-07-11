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
#include "instruments/array_instrument.hpp"
#include "knowledge/knowledge_database.hpp"
#include "observation/data_archive.hpp"
#include "observation/session_scheduler.hpp"
#include "ui/tabs/planetarium_tab.hpp"

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

} // anonymous namespace

namespace parallax::core
{

Application::Application()
    : m_observer_registry(astro::ObserverRegistry::create_default())
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

    // 7b. PanelSystem (reused by shell chrome rendering)
    m_panel_system.init(*m_context, m_pipeline->get_render_pass(), shader_dir);

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

    m_array_instrument = std::make_unique<instruments::ArrayInstrument>(
        instruments::ArrayInstrument::create_default());
    m_analyzer = std::make_unique<analysis::MockAnalyzer>();

    // 9. Simulation time
    m_julian_date = astro::TimeSystem::now_as_jd();
    m_time_scale = 1.0;
    {
        const astro::ObserverLocation& active_observer = m_observer_registry.get_active();
        const auto dt = astro::TimeSystem::from_julian_date(m_julian_date);
        PLX_CORE_INFO("Simulation start: JD {:.6f} ({:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:04.1f} UTC)",
                      m_julian_date, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
        PLX_CORE_INFO("Observer: {} ({:.2f}, {:.2f})",
                      active_observer.name,
                      glm::degrees(active_observer.latitude_rad),
                      glm::degrees(active_observer.longitude_rad));
    }

    m_selection = std::make_unique<ui::Selection>();
    m_shell = std::make_unique<ui::shell::Shell>(m_hud->get_font(),
                                                  *m_line_renderer,
                                                  m_panel_system,
                                                  *m_context,
                                                  *m_swapchain,
                                                  m_pipeline->get_render_pass(),
                                                  shader_dir,
                                                  *m_universe,
                                                  *m_array_instrument,
                                                  *m_knowledge,
                                                  *m_archive,
                                                  *m_scheduler,
                                                  m_observer_registry,
                                                  *m_selection,
                                                  *m_input,
                                                  m_julian_date,
                                                  m_time_scale);
    m_shell->set_callbacks({
        .on_pause_toggle = [this]()
        {
            m_time_scale = (m_time_scale == 0.0) ? 1.0 : 0.0;
        },
        .on_time_scale_set = [this](const f64 scale)
        {
            m_time_scale = scale;
        },
        .on_time_scale_up = [this]()
        {
            if (m_time_scale == 0.0)
            {
                m_time_scale = 1.0;
            }
            else
            {
                m_time_scale *= 2.0;
            }
        },
        .on_time_scale_down = [this]()
        {
            if (m_time_scale == 0.0)
            {
                m_time_scale = 1.0;
            }
            else
            {
                m_time_scale *= 0.5;
            }
        },
        .on_time_reset = [this]()
        {
            m_julian_date = astro::TimeSystem::now_as_jd();
            m_time_scale = 1.0;
        }
    });
    m_shell->init();

    // 12-14. Command pool, sync, frame time
    create_command_pool();
    create_command_buffers();
    create_sync_objects();
    m_last_frame_time = std::chrono::steady_clock::now();

    PLX_CORE_INFO("Application initialized — shell integration active");
}

// =================================================================
// Shutdown
// =================================================================

void Application::shutdown()
{
    if (m_shell)
    {
        m_shell->save_layout();
    }

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
    m_array_instrument.reset();
    m_shell.reset();
    m_archive.reset();
    m_scheduler.reset();
    m_knowledge.reset();
    m_selection.reset();

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
    return m_shell ? m_shell->get_planetarium_tab().is_atmosphere_on() : true;
}

void Application::toggle_atmosphere()
{
    if (m_shell)
    {
        m_shell->get_planetarium_tab().toggle_atmosphere();
    }
}

void Application::set_atmosphere(bool on)
{
    if (m_shell)
    {
        m_shell->get_planetarium_tab().set_atmosphere(on);
    }
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
    if (!m_shell)
    {
        return;
    }

    ui::shell::InputEvent event{};
    event.mouse_pos = m_input->get_mouse_position();
    event.mouse_delta = m_input->get_mouse_drag_delta();
    event.is_dragging = m_input->is_mouse_dragging();
    event.drag_delta = m_input->get_mouse_drag_delta();
    event.scroll_delta = m_input->get_scroll_delta();

    const VkExtent2D extent = m_swapchain->get_extent();
    const ui::shell::ViewportRect window{
        .x = 0,
        .y = 0,
        .width = extent.width,
        .height = extent.height
    };
    event.inside_viewport = window.contains(event.mouse_pos);

    if (m_input->was_click())
    {
        event.was_click = true;
        event.click_button = ui::shell::MouseButton::Left;
        event.click_pos = m_input->get_click_position();
    }

    const bool consumed = m_shell->on_input(event);
    m_input->set_cursor(consumed ? CursorStyle::Hand : (event.is_dragging ? CursorStyle::SizeAll : CursorStyle::Crosshair));

    if (m_input->is_key_pressed(SDL_SCANCODE_ESCAPE))
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
    m_julian_date += (delta_time_sec * m_time_scale) / 86400.0;

    if (m_scheduler && m_universe)
    {
        if (m_array_instrument)
        {
            m_scheduler->update(m_julian_date, delta_time_sec, *m_universe, *m_array_instrument);
        }

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

            if (m_shell)
            {
                m_shell->push_notification(
                    std::format("Observation session {} completed", session_id),
                    ui::shell::NotificationSeverity::Info);
            }
        }
    }

    if (m_shell)
    {
        m_shell->update(delta_time_sec);
    }
}

// =================================================================
// record_command_buffer — GPU draw order
//
// 1. Sky background (fullscreen triangle)
// 2. Starfield (instanced points, additive)
// 3. Sky overlay lines: grid → constellations → DSOs → horizon → selection indicator
// 4. Panel backgrounds (transparent filled quads)
// 5. UI border lines: toolbar / side panel / workflow panel borders
// 6. All text: HUD + toolbar + side panel + workflow panels (batched, single draw call)
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

    if (m_shell)
    {
        m_shell->render(cmd, {
            .x = 0,
            .y = 0,
            .width = extent.width,
            .height = extent.height
        });
    }

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
