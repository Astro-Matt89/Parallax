/// @file user_data_path.cpp
/// @brief User-specific save directory resolution helpers.

// MSVC flags std::getenv as "unsafe" (C4996) and our build promotes warnings
// to errors. The usage here is read-only and the returned pointer is consumed
// immediately, which is safe; disable the deprecation for this TU only.
#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS 1
#  endif
#  pragma warning(disable : 4996)
#endif

#include "core/user_data_path.hpp"

#include <cstdlib>

namespace parallax::core
{

namespace
{
    /// @brief Cross-platform getenv wrapper that avoids MSVC's deprecation warning.
    [[nodiscard]] const char* safe_getenv(const char* name) noexcept
    {
#ifdef _MSC_VER
        return std::getenv(name);  // C4996 disabled above for this TU.
#else
        return std::getenv(name);
#endif
    }
}

std::filesystem::path user_data_save_dir() noexcept
{
    std::filesystem::path path = "./save";

#ifdef _WIN32
    if (const char* appdata = safe_getenv("APPDATA"); appdata != nullptr)
    {
        path = std::filesystem::path(appdata) / "Parallax" / "save";
    }
    else if (const char* userprofile = safe_getenv("USERPROFILE"); userprofile != nullptr)
    {
        path = std::filesystem::path(userprofile) / "AppData" / "Roaming" / "Parallax" / "save";
    }
#elif defined(__APPLE__)
    if (const char* home = safe_getenv("HOME"); home != nullptr)
    {
        path = std::filesystem::path(home) / "Library" / "Application Support" / "Parallax" / "save";
    }
#else
    if (const char* xdg = safe_getenv("XDG_DATA_HOME"); xdg != nullptr)
    {
        path = std::filesystem::path(xdg) / "parallax" / "save";
    }
    else if (const char* home = safe_getenv("HOME"); home != nullptr)
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
