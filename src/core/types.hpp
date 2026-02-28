#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <cstdint>

namespace parallax
{
    // Precision aliases
    using f32 = float;
    using f64 = double;
    using u8  = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    using i32 = int32_t;
    using i64 = int64_t;

    // Vector types (double precision for astronomy)
    using Vec2d = glm::dvec2;
    using Vec3d = glm::dvec3;
    using Vec4d = glm::dvec4;
    using Mat3d = glm::dmat3;
    using Mat4d = glm::dmat4;

    // Vector types (float for rendering)
    using Vec2f = glm::vec2;
    using Vec3f = glm::vec3;
    using Vec4f = glm::vec4;
    using Mat4f = glm::mat4;

    // Astronomical constants
    namespace astro_constants
    {
        constexpr f64 kPi          = glm::pi<f64>();
        constexpr f64 kTwoPi       = 2.0 * kPi;
        constexpr f64 kHalfPi      = kPi / 2.0;
        constexpr f64 kDegToRad    = kPi / 180.0;
        constexpr f64 kRadToDeg    = 180.0 / kPi;
        constexpr f64 kHourToRad   = kPi / 12.0;
        constexpr f64 kRadToHour   = 12.0 / kPi;
        constexpr f64 kArcSecToRad = kPi / (180.0 * 3600.0);
        constexpr f64 kJ2000       = 2451545.0;  // Julian Date of J2000.0 epoch
    }
}