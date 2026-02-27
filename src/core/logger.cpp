/// @file logger.cpp
/// @brief Logger implementation — dual spdlog loggers with console + rotating file sinks.

#include "core/logger.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <vector>

namespace parallax::core
{

// ---- Static member definitions ----
std::shared_ptr<spdlog::logger> Logger::s_core_logger;
std::shared_ptr<spdlog::logger> Logger::s_app_logger;

void Logger::init()
{
    // -----------------------------------------------------------------
    // Shared sinks — both loggers write to the same console and file
    // -----------------------------------------------------------------

    // Console sink with color output
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%T.%e] [%n] [%^%l%$] %v");

    // Rotating file sink: 5 MB max size, 3 rotated files
    constexpr std::size_t kMaxFileSize = 5 * 1024 * 1024; // 5 MB
    constexpr std::size_t kMaxFiles = 3;
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "parallax.log", kMaxFileSize, kMaxFiles);
    file_sink->set_pattern("[%T.%e] [%n] [%^%l%$] %v");

    // -----------------------------------------------------------------
    // Core logger ("PARALLAX") — engine internals
    // -----------------------------------------------------------------
    std::vector<spdlog::sink_ptr> core_sinks{console_sink, file_sink};
    s_core_logger = std::make_shared<spdlog::logger>("PARALLAX", core_sinks.begin(), core_sinks.end());
    s_core_logger->set_level(spdlog::level::trace);
    s_core_logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(s_core_logger);

    // -----------------------------------------------------------------
    // App logger ("APP") — gameplay, user-facing
    // -----------------------------------------------------------------
    std::vector<spdlog::sink_ptr> app_sinks{console_sink, file_sink};
    s_app_logger = std::make_shared<spdlog::logger>("APP", app_sinks.begin(), app_sinks.end());
    s_app_logger->set_level(spdlog::level::trace);
    s_app_logger->flush_on(spdlog::level::warn);
    spdlog::register_logger(s_app_logger);
}

void Logger::shutdown()
{
    s_core_logger.reset();
    s_app_logger.reset();
    spdlog::drop_all();
    spdlog::shutdown();
}

std::shared_ptr<spdlog::logger>& Logger::get_core_logger()
{
    return s_core_logger;
}

std::shared_ptr<spdlog::logger>& Logger::get_app_logger()
{
    return s_app_logger;
}

} // namespace parallax::core