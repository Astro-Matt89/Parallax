/// @file user_data_path.cpp
/// @brief User-specific save directory resolution helpers.

#include "core/user_data_path.hpp"

#include <cstdlib>

namespace parallax::core
{

std::filesystem::path user_data_save_dir() noexcept
{
    std::filesystem::path path = "./save";

#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr)
    {
        path = std::filesystem::path(appdata) / "Parallax" / "save";
    }
    else if (const char* userprofile = std::getenv("USERPROFILE"); userprofile != nullptr)
    {
        path = std::filesystem::path(userprofile) / "AppData" / "Roaming" / "Parallax" / "save";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr)
    {
        path = std::filesystem::path(home) / "Library" / "Application Support" / "Parallax" / "save";
    }
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr)
    {
        path = std::filesystem::path(xdg) / "parallax" / "save";
    }
    else if (const char* home = std::getenv("HOME"); home != nullptr)
    {
        path = std::filesystem::path(home) / ".local" / "share" / "parallax" / "save";
    }
#endif

    try
    {
        std::filesystem::create_directories(path);
    }
    catch (...)
    {
    }

    return path;
}

} // namespace parallax::core
