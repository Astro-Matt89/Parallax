#pragma once

#include "core/types.hpp"

#include <string>

namespace parallax::astro
{
    /// Which celestial body an observer sits on top of (or in orbit around).
    enum class ParentBody : u8
    {
        Earth = 0,
        Moon  = 1,
        Mars  = 2,
        Sun   = 3,
        Space = 4
    };

    [[nodiscard]] constexpr const char* to_string(ParentBody body) noexcept
    {
        switch (body)
        {
            case ParentBody::Earth: return "Earth";
            case ParentBody::Moon:  return "Moon";
            case ParentBody::Mars:  return "Mars";
            case ParentBody::Sun:   return "Sun";
            case ParentBody::Space: return "Space";
        }
        return "Unknown";
    }

    /// @brief Observer geographic location.
    struct ObserverLocation
    {
        f64 latitude_rad = 0.0;   ///< Geographic latitude (radians, north positive)
        f64 longitude_rad = 0.0;  ///< Geographic longitude (radians, east positive)
        f64 elevation_m = 0.0;    ///< Elevation above parent-body reference surface (metres)

        // New in Sprint 09 Task 9.8:
        std::string name{};                         ///< Display name (e.g. "Tycho Crater Base").
        ParentBody parent_body{ParentBody::Earth};
        bool has_atmosphere{true};
        f32 bortle_scale{4.0f};                     ///< Meaningful only when has_atmosphere == true.
    };
}
