/// @file main.cpp
/// @brief Parallax entry point.

#define SDL_MAIN_HANDLED

#include "core/logger.hpp"
#include "core/window.hpp"
#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"

#include <cstdlib>

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    {
        // Window
        parallax::core::Window window({.title = "Parallax", .width = 1280, .height = 720});

        // Vulkan context
        parallax::vulkan::Context context(
            {.app_name = "Parallax", .enable_validation = true},
            window);

        // Swapchain
        parallax::vulkan::Swapchain swapchain(context, window.get_width(), window.get_height());

        // Main loop
        while (!window.should_close())
        {
            window.poll_events();

            // Handle resize
            if (window.was_resized())
            {
                uint32_t w = window.get_width();
                uint32_t h = window.get_height();

                if (w > 0 && h > 0)
                {
                    swapchain.recreate(w, h);
                    PLX_CORE_INFO("Swapchain recreated: {}x{}", w, h);
                }
            }
        }

        context.wait_idle();
    }

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return EXIT_SUCCESS;
}