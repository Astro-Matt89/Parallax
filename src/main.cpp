/// @file main.cpp
/// @brief Parallax entry point.

#include "core/logger.hpp"

#include <cstdlib>

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    // Task 1.2 acceptance: verify both loggers output to console and file
    PLX_CORE_TRACE("Core logger trace test");
    PLX_CORE_INFO("Core logger info test");
    PLX_CORE_WARN("Core logger warn test");
    PLX_INFO("App logger info test");

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return EXIT_SUCCESS;
}