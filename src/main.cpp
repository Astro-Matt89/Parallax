/// @file main.cpp
/// @brief Parallax entry point.

#define SDL_MAIN_HANDLED

#include "core/logger.hpp"
#include "core/window.hpp"

#include <cstdlib>

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    // -----------------------------------------------------------------
    // Task 1.3 acceptance: window opens, stays open, closes on X button
    // -----------------------------------------------------------------
    {
        parallax::core::Window window({.title = "Parallax", .width = 1280, .height = 720});

        auto extensions = window.get_required_vulkan_extensions();
        PLX_CORE_INFO("Vulkan extensions required: {}", extensions.size());

        while (!window.should_close())
        {
            window.poll_events();

            if (window.was_resized())
            {
                PLX_CORE_INFO("Framebuffer now: {}x{}", window.get_width(), window.get_height());
            }
        }
    }

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return EXIT_SUCCESS;
}