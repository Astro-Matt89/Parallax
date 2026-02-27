#pragma once

/// @file logger.hpp
/// @brief Dual-logger system wrapping spdlog (engine + application loggers).

#include <spdlog/spdlog.h>

#include <memory>

namespace parallax::core
{
    /// @brief Centralized logging facility for Parallax.
    ///
    /// Provides two separate loggers:
    /// - **PARALLAX** (core): engine internals, Vulkan, subsystems
    /// - **APP**: gameplay, observatory, user-facing messages
    ///
    /// Both write to colored console output and a rotating log file.
    /// Call init() once from main() before any logging.
    class Logger
    {
    public:
        /// @brief Initialize both loggers with console + file sinks.
        /// Must be called once at startup before any PLX_ macros are used.
        static void init();

        /// @brief Flush and tear down all loggers.
        /// Call once at shutdown after all logging is complete.
        static void shutdown();

        /// @brief Access the engine-internal logger ("PARALLAX").
        [[nodiscard]] static std::shared_ptr<spdlog::logger>& get_core_logger();

        /// @brief Access the application-level logger ("APP").
        [[nodiscard]] static std::shared_ptr<spdlog::logger>& get_app_logger();

    private:
        static std::shared_ptr<spdlog::logger> s_core_logger;
        static std::shared_ptr<spdlog::logger> s_app_logger;
    };

} // namespace parallax::core

// -----------------------------------------------------------------
// Core engine log macros
// -----------------------------------------------------------------
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define PLX_CORE_TRACE(...)    ::parallax::core::Logger::get_core_logger()->trace(__VA_ARGS__)
#define PLX_CORE_INFO(...)     ::parallax::core::Logger::get_core_logger()->info(__VA_ARGS__)
#define PLX_CORE_WARN(...)     ::parallax::core::Logger::get_core_logger()->warn(__VA_ARGS__)
#define PLX_CORE_ERROR(...)    ::parallax::core::Logger::get_core_logger()->error(__VA_ARGS__)
#define PLX_CORE_CRITICAL(...) ::parallax::core::Logger::get_core_logger()->critical(__VA_ARGS__)

// -----------------------------------------------------------------
// Application log macros
// -----------------------------------------------------------------
#define PLX_TRACE(...)         ::parallax::core::Logger::get_app_logger()->trace(__VA_ARGS__)
#define PLX_INFO(...)          ::parallax::core::Logger::get_app_logger()->info(__VA_ARGS__)
#define PLX_WARN(...)          ::parallax::core::Logger::get_app_logger()->warn(__VA_ARGS__)
#define PLX_ERROR(...)         ::parallax::core::Logger::get_app_logger()->error(__VA_ARGS__)
#define PLX_CRITICAL(...)      ::parallax::core::Logger::get_app_logger()->critical(__VA_ARGS__)
// NOLINTEND(cppcoreguidelines-macro-usage)