/// @file main.cpp
/// @brief Parallax entry point.

#define SDL_MAIN_HANDLED

#include "core/logger.hpp"
#include "core/window.hpp"
#include "vulkan/context.hpp"

#include <cstdlib>

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    {
        // Task 1.3: Window
        parallax::core::Window window({.title = "Parallax", .width = 1280, .height = 720});

        // Task 1.4: Vulkan context — instance, surface, device, queues
        parallax::vulkan::Context context(
            {.app_name = "Parallax", .enable_validation = true},
            window);

        // Main loop — just poll events until close
        while (!window.should_close())
        {
            window.poll_events();
        }

        context.wait_idle();
    }

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return EXIT_SUCCESS;
}