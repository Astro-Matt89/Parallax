/// @file main.cpp
/// @brief Parallax entry point.

#define SDL_MAIN_HANDLED

#include "core/application.hpp"
#include "core/logger.hpp"

#include <cstdlib>

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    {
        parallax::core::Application app;
        app.run();
    }

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return EXIT_SUCCESS;
}