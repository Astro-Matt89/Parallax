/// @file application.cpp
/// @brief Application implementation — init, main loop, frame rendering, shutdown.

#include "core/application.hpp"

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

    // 2. Vulkan context (instance, surface, device, queues)
    m_context = std::make_unique<vulkan::Context>(
        vulkan::ContextConfig{.app_name = "Parallax", .enable_validation = true},
        *m_window);

    // 3. Swapchain
    m_swapchain = std::make_unique<vulkan::Swapchain>(
        *m_context, m_window->get_width(), m_window->get_height());

    // 4. Pipeline (render pass + graphics pipeline + framebuffers)
    std::filesystem::path shader_dir{PLX_SHADER_DIR};
    PLX_CORE_INFO("Shader directory: {}", shader_dir.string());
    m_pipeline = std::make_unique<vulkan::Pipeline>(*m_context, *m_swapchain, shader_dir);

    // 5. Command pool + buffers
    create_command_pool();
    create_command_buffers();

    // 6. Synchronization objects
    create_sync_objects();

    PLX_CORE_INFO("Application initialized — all subsystems ready");
}

// =================================================================
// Shutdown
// =================================================================

void Application::shutdown()
{
    if (!m_context)
    {
        return;
    }

    m_context->wait_idle();

    destroy_sync_objects();

    // Command pool (implicitly frees command buffers)
    if (m_command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_context->get_device(), m_command_pool, nullptr);
        m_command_pool = VK_NULL_HANDLE;
        PLX_CORE_TRACE("Command pool destroyed");
    }

    // Reverse creation order: pipeline → swapchain → context → window
    m_pipeline.reset();
    m_swapchain.reset();
    m_context.reset();
    m_window.reset();
}

// =================================================================
// Main loop
// =================================================================

void Application::main_loop()
{
    while (!m_window->should_close())
    {
        m_window->poll_events();

        if (m_window->was_resized())
        {
            m_framebuffer_resized = true;
        }

        // Skip drawing when minimized (zero extent)
        if (m_window->get_width() == 0 || m_window->get_height() == 0)
        {
            continue;
        }

        draw_frame();
    }

    m_context->wait_idle();
}

// =================================================================
// Frame rendering
// =================================================================

void Application::draw_frame()
{
    VkDevice device = m_context->get_device();

    // -----------------------------------------------------------------
    // 1. Wait for this frame slot's fence (previous use of this slot)
    // -----------------------------------------------------------------
    check_vk(
        vkWaitForFences(device, 1, &m_in_flight_fences[m_current_frame], VK_TRUE, std::numeric_limits<uint64_t>::max()),
        "vkWaitForFences");

    // -----------------------------------------------------------------
    // 2. Acquire next swapchain image
    // -----------------------------------------------------------------
    uint32_t image_index = 0;
    VkResult acquire_result = vkAcquireNextImageKHR(
        device,
        m_swapchain->get_handle(),
        std::numeric_limits<uint64_t>::max(),
        m_image_available_semaphores[m_current_frame],
        VK_NULL_HANDLE,
        &image_index);

    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreate_swapchain();
        return;
    }
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR)
    {
        PLX_CORE_CRITICAL("Failed to acquire swapchain image: {}", static_cast<int>(acquire_result));
        std::abort();
    }

    // Only reset the fence if we are actually going to submit work
    check_vk(vkResetFences(device, 1, &m_in_flight_fences[m_current_frame]), "vkResetFences");

    // -----------------------------------------------------------------
    // 3. Record command buffer
    // -----------------------------------------------------------------
    check_vk(
        vkResetCommandBuffer(m_command_buffers[m_current_frame], 0),
        "vkResetCommandBuffer");

    record_command_buffer(m_command_buffers[m_current_frame], image_index);

    // -----------------------------------------------------------------
    // 4. Submit to graphics queue
    //    Wait on: image_available (per frame-in-flight slot)
    //    Signal:  render_finished (per swapchain image — safe for present reuse)
    //    Signal:  in_flight_fence (per frame-in-flight slot — CPU sync)
    // -----------------------------------------------------------------
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

    check_vk(
        vkQueueSubmit(m_context->get_graphics_queue(), 1, &submit_info, m_in_flight_fences[m_current_frame]),
        "vkQueueSubmit");

    // -----------------------------------------------------------------
    // 5. Present
    //    Wait on: render_finished (same per-image semaphore we just signaled)
    // -----------------------------------------------------------------
    VkSwapchainKHR swapchains[] = {m_swapchain->get_handle()};

    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swapchains;
    present_info.pImageIndices = &image_index;

    VkResult present_result = vkQueuePresentKHR(m_context->get_present_queue(), &present_info);

    if (present_result == VK_ERROR_OUT_OF_DATE_KHR
        || present_result == VK_SUBOPTIMAL_KHR
        || m_framebuffer_resized)
    {
        m_framebuffer_resized = false;
        recreate_swapchain();
    }
    else if (present_result != VK_SUCCESS)
    {
        PLX_CORE_CRITICAL("Failed to present swapchain image: {}", static_cast<int>(present_result));
        std::abort();
    }

    // -----------------------------------------------------------------
    // 6. Advance frame-in-flight index
    // -----------------------------------------------------------------
    m_current_frame = (m_current_frame + 1) % kMaxFramesInFlight;
}

// =================================================================
// Command buffer recording
// =================================================================

void Application::record_command_buffer(VkCommandBuffer cmd, uint32_t image_index)
{
    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    check_vk(vkBeginCommandBuffer(cmd, &begin_info), "vkBeginCommandBuffer");

    // Clear to near-black with a hint of deep blue
    VkClearValue clear_color{};
    clear_color.color = {{0.0f, 0.0f, 0.02f, 1.0f}};

    VkExtent2D extent = m_swapchain->get_extent();

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = m_pipeline->get_render_pass();
    render_pass_info.framebuffer = m_pipeline->get_framebuffer(image_index);
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->get_pipeline());

    // Dynamic viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    // Dynamic scissor
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // Draw 1 vertex = 1 point (the test star)
    vkCmdDraw(cmd, 1, 1, 0, 0);

    vkCmdEndRenderPass(cmd);

    check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
}

// =================================================================
// Swapchain recreation
// =================================================================

void Application::recreate_swapchain()
{
    m_context->wait_idle();

    uint32_t w = m_window->get_width();
    uint32_t h = m_window->get_height();

    if (w == 0 || h == 0)
    {
        return;
    }

    // Destroy old per-image semaphores before swapchain recreation changes image count
    VkDevice device = m_context->get_device();
    for (auto sem : m_render_finished_semaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }
    m_render_finished_semaphores.clear();

    m_swapchain->recreate(w, h);
    m_pipeline->recreate_framebuffers(*m_swapchain);

    // Recreate per-image semaphores for the new swapchain image count
    uint32_t image_count = m_swapchain->get_image_count();
    m_render_finished_semaphores.resize(image_count);

    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for (uint32_t i = 0; i < image_count; ++i)
    {
        check_vk(
            vkCreateSemaphore(device, &sem_info, nullptr, &m_render_finished_semaphores[i]),
            "vkCreateSemaphore (render finished, recreate)");
    }

    PLX_CORE_INFO("Swapchain + framebuffers recreated: {}x{} ({} images)", w, h, image_count);
}

// =================================================================
// Command pool + buffers
// =================================================================

void Application::create_command_pool()
{
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = m_context->get_graphics_queue_family();

    check_vk(
        vkCreateCommandPool(m_context->get_device(), &pool_info, nullptr, &m_command_pool),
        "vkCreateCommandPool");

    PLX_CORE_INFO("Command pool created");
}

void Application::create_command_buffers()
{
    m_command_buffers.resize(kMaxFramesInFlight);

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = m_command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = kMaxFramesInFlight;

    check_vk(
        vkAllocateCommandBuffers(m_context->get_device(), &alloc_info, m_command_buffers.data()),
        "vkAllocateCommandBuffers");

    PLX_CORE_INFO("Command buffers allocated: {}", kMaxFramesInFlight);
}

// =================================================================
// Synchronization objects
// =================================================================

void Application::create_sync_objects()
{
    VkDevice device = m_context->get_device();

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Per-frame-in-flight: image_available semaphores + fences
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        check_vk(
            vkCreateSemaphore(device, &semaphore_info, nullptr, &m_image_available_semaphores[i]),
            "vkCreateSemaphore (image available)");
        check_vk(
            vkCreateFence(device, &fence_info, nullptr, &m_in_flight_fences[i]),
            "vkCreateFence (in flight)");
    }

    // Per-swapchain-image: render_finished semaphores
    uint32_t image_count = m_swapchain->get_image_count();
    m_render_finished_semaphores.resize(image_count);

    for (uint32_t i = 0; i < image_count; ++i)
    {
        check_vk(
            vkCreateSemaphore(device, &semaphore_info, nullptr, &m_render_finished_semaphores[i]),
            "vkCreateSemaphore (render finished)");
    }

    PLX_CORE_INFO("Sync objects created: {} frames in flight, {} image semaphores",
                  kMaxFramesInFlight, image_count);
}

void Application::destroy_sync_objects()
{
    VkDevice device = m_context->get_device();

    for (auto sem : m_render_finished_semaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
        }
    }
    m_render_finished_semaphores.clear();

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (m_image_available_semaphores[i] != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, m_image_available_semaphores[i], nullptr);
            m_image_available_semaphores[i] = VK_NULL_HANDLE;
        }
        if (m_in_flight_fences[i] != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, m_in_flight_fences[i], nullptr);
            m_in_flight_fences[i] = VK_NULL_HANDLE;
        }
    }

    PLX_CORE_TRACE("Sync objects destroyed");
}

} // namespace parallax::core