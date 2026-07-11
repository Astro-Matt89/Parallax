#pragma once

/// @file application.hpp
/// @brief Main application class — lifecycle, main loop, frame rendering.

#include "astro/atmosphere.hpp"
#include "astro/coordinates.hpp"
#include "astro/observer_registry.hpp"
#include "astro/time_system.hpp"
#include "core/input.hpp"
#include "core/types.hpp"
#include "core/window.hpp"
#include "ui/hud.hpp"
#include "ui/panel_system.hpp"                            // ← SPRINT 05 Task 5.1
#include "ui/selection.hpp"
#include "ui/shell/shell.hpp"
#include "universe/universe.hpp"                          // ← SPRINT 07 Task 7.7
#include "vulkan/context.hpp"
#include "vulkan/pipeline.hpp"
#include "vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace parallax::analysis { class MockAnalyzer; }
namespace parallax::instruments { class ArrayInstrument; }
namespace parallax::rendering { class LineRenderer; }

namespace parallax::knowledge
{
    class KnowledgeDatabase;
}

namespace parallax::observation
{
    class DataArchive;
    class SessionScheduler;
}

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

        // -----------------------------------------------------------------
        // Atmosphere toggle API                          ← SPRINT 06 Task 6.7
        // -----------------------------------------------------------------

        /// @brief Returns the user's atmosphere preference toggle.
        [[nodiscard]] bool is_atmosphere_on() const;

        /// @brief Toggle atmosphere on/off and log the change.
        void toggle_atmosphere();

        /// @brief Explicitly set atmosphere state.
        void set_atmosphere(bool on);

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

        u32 m_frame_counter = 0;  ///< Frame counter for periodic logging

        // -----------------------------------------------------------------
        // Subsystems (created in init order, destroyed in reverse)
        // -----------------------------------------------------------------
        std::unique_ptr<Window> m_window;
        std::unique_ptr<vulkan::Context> m_context;
        std::unique_ptr<vulkan::Swapchain> m_swapchain;
        std::unique_ptr<vulkan::Pipeline> m_pipeline;       ///< Render pass + framebuffers (from Sprint 01)
        std::unique_ptr<rendering::LineRenderer> m_line_renderer;  // UI borders / shell chrome
        std::unique_ptr<Input> m_input;
        std::unique_ptr<ui::Hud> m_hud;                     ///< Retro HUD overlay  ← SPRINT 03 Task 3.6
        std::unique_ptr<ui::Selection> m_selection;
        std::unique_ptr<ui::shell::Shell> m_shell;

        // -----------------------------------------------------------------
        // Universe facade                                   ← SPRINT 07 Task 7.7
        // -----------------------------------------------------------------

        /// @brief Universe facade over all data providers (stars, DSOs, SS, procedural).
        std::unique_ptr<universe::Universe> m_universe;

        /// @brief Player's knowledge state — drives rendering style and encyclopedia details.
        ///                                                              ← SPRINT 08 Task 8.9
        std::unique_ptr<knowledge::KnowledgeDatabase> m_knowledge;
        std::unique_ptr<observation::SessionScheduler> m_scheduler;
        std::unique_ptr<observation::DataArchive> m_archive;
        std::unique_ptr<instruments::ArrayInstrument> m_array_instrument;
        std::unique_ptr<analysis::MockAnalyzer> m_analyzer;

        // -----------------------------------------------------------------
        // UI subsystems                                     ← SPRINT 09
        // -----------------------------------------------------------------
        ui::PanelSystem m_panel_system;                      // Reused by shell chrome rendering

        // -----------------------------------------------------------------
        // Simulation state
        // -----------------------------------------------------------------
        f64 m_julian_date = 0.0;            ///< Current simulation time (JD)
        f64 m_time_scale = 1.0;             ///< 1.0 = real-time, 0.0 = paused
        f64 m_delta_time = 0.0;             ///< Last frame delta for FPS display
        astro::ObserverRegistry m_observer_registry;

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
