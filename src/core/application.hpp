#pragma once

/// @file application.hpp
/// @brief Main application class — lifecycle, main loop, frame rendering.

#include "astro/coordinates.hpp"
#include "astro/time_system.hpp"
#include "catalog/star_entry.hpp"
#include "core/input.hpp"
#include "core/types.hpp"
#include "core/window.hpp"
#include "rendering/camera.hpp"
#include "rendering/starfield.hpp"
#include "vulkan/context.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace parallax::core
{
    /// @brief Top-level application class that owns all subsystems and drives the main loop.
    ///
    /// Lifecycle: init() in constructor → run() drives main_loop() → shutdown() in destructor.
    /// Frame rendering uses 2 frames in flight with per-frame fences and semaphores.
    /// Render-finished semaphores are per-swapchain-image to avoid reuse conflicts
    /// with the presentation engine.
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
        void destroy_sync_objects();

        void process_input();
        void update_simulation(f64 delta_time_sec);

        void record_command_buffer(VkCommandBuffer cmd, uint32_t image_index);

        static constexpr uint32_t kMaxFramesInFlight = 2;

        // -----------------------------------------------------------------
        // Subsystems (created in init order, destroyed in reverse)
        // -----------------------------------------------------------------
        std::unique_ptr<Window> m_window;
        std::unique_ptr<vulkan::Context> m_context;
        std::unique_ptr<vulkan::Swapchain> m_swapchain;
        std::unique_ptr<vulkan::Pipeline> m_pipeline;       ///< Render pass + framebuffers (from Sprint 01)
        std::unique_ptr<rendering::Starfield> m_starfield;
        std::unique_ptr<rendering::Camera> m_camera;
        std::unique_ptr<Input> m_input;

        // -----------------------------------------------------------------
        // Star catalog
        // -----------------------------------------------------------------
        std::vector<catalog::StarEntry> m_stars;

        // -----------------------------------------------------------------
        // Simulation state
        // -----------------------------------------------------------------
        f64 m_julian_date = 0.0;            ///< Current simulation time (JD)
        f64 m_time_scale = 1.0;             ///< 1.0 = real-time, 0.0 = paused
        astro::ObserverLocation m_observer;  ///< Observer geographic location

        /// @brief Wall-clock time tracking for delta_time computation.
        std::chrono::steady_clock::time_point m_last_frame_time;

        // -----------------------------------------------------------------
        // Command submission
        // -----------------------------------------------------------------
        VkCommandPool m_command_pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_command_buffers;

        // -----------------------------------------------------------------
        // Per-frame synchronization (indexed by frame-in-flight slot)
        // -----------------------------------------------------------------
        std::array<VkSemaphore, kMaxFramesInFlight> m_image_available_semaphores{};
        std::array<VkFence, kMaxFramesInFlight> m_in_flight_fences{};

        // -----------------------------------------------------------------
        // Per-swapchain-image synchronization
        // Render-finished semaphores are indexed by swapchain image to avoid
        // reuse while the presentation engine still holds the semaphore.
        // -----------------------------------------------------------------
        std::vector<VkSemaphore> m_render_finished_semaphores;

        uint32_t m_current_frame = 0;
        bool m_framebuffer_resized = false;
    };

} // namespace parallax::core