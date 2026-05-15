#pragma once

/// @file user_data_path.hpp
/// @brief User-specific save directory resolution helpers.

#include <filesystem>

namespace parallax::core
{

/// @brief Resolve and create the user save directory for Parallax.
///
/// This helper never throws across the API boundary. It attempts to create
/// the resolved directory and always returns the chosen path.
[[nodiscard]] std::filesystem::path user_data_save_dir() noexcept;

} // namespace parallax::core
