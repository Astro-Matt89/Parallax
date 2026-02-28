#pragma once

/// @file application.hpp
/// @brief Main application class — lifecycle, main loop, frame rendering.

#include "core/window.hpp"
#include "vulkan/context.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace parallax::core
{
    /// @brief Top-level application class that owns all subsystems and drives the main loop.
    ///
    /// Lifecycle: init() in constructor → run() drives main_loop() → shutdown() in destructor.
    /// Frame rendering uses 2 frames in flight with per-frame fences and semaphores.
    class Application
    {
    public:
        /// @brief Initialize all subsystems: window, Vulkan context, swapchain, pipeline, sync.
        Application();

        /// @brief Shut down all subsystems in reverse creation order.
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /// @brief Enter the main loop. Returns when the window is closed.
        void run();

    private:
        void init();
        void main_loop();
        void shutdown();

        void draw_frame();
        void recreate_swapchain();

        void create_command_pool();
        void create_command_buffers();
        void create_sync_objects();

        void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

        static constexpr uint32_t kMaxFramesInFlight = 2;

        // -----------------------------------------------------------------
        // Subsystems (created in init order, destroyed in reverse)
        // -----------------------------------------------------------------
        std::unique_ptr<Window> m_window;
        std::unique_ptr<vulkan::Context> m_context;
        std::unique_ptr<vulkan::Swapchain> m_swapchain;
        std::unique_ptr<vulkan::Pipeline> m_pipeline;

        // -----------------------------------------------------------------
        // Command submission
        // -----------------------------------------------------------------
        VkCommandPool m_command_pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_command_buffers;

        // -----------------------------------------------------------------
        // Per-frame synchronization (2 frames in flight)
        // -----------------------------------------------------------------
        std::array<VkSemaphore, kMaxFramesInFlight> m_image_available_semaphores{};
        std::array<VkSemaphore, kMaxFramesInFlight> m_render_finished_semaphores{};
        std::array<VkFence, kMaxFramesInFlight> m_in_flight_fences{};

        uint32_t m_current_frame = 0;
        bool m_framebuffer_resized = false;
    };

} // namespace parallax::core