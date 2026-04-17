/// @file solar_system_renderer_helpers.cpp
/// @brief Pure static helper functions for SolarSystemRenderer.
///
/// Split from solar_system_renderer.cpp to enable testing without
/// a Vulkan device. No rendering dependencies — only GLM types.
///
/// SPRINT 06 Task 6.5

#include "rendering/solar_system_renderer.hpp"

#include <cmath>

namespace parallax::rendering
{

Vec4f SolarSystemRenderer::planet_color(u32 planet_id)
{
    using namespace astro::planet_id;
    switch (planet_id)
    {
        case kMercury: return {0.75f, 0.75f, 0.75f, 1.0f};
        case kVenus:   return {1.00f, 0.95f, 0.80f, 1.0f};
        case kMars:    return {0.95f, 0.45f, 0.30f, 1.0f};
        case kJupiter: return {0.95f, 0.87f, 0.70f, 1.0f};
        case kSaturn:  return {0.93f, 0.85f, 0.55f, 1.0f};
        case kUranus:  return {0.70f, 0.90f, 0.95f, 1.0f};
        case kNeptune: return {0.45f, 0.60f, 0.95f, 1.0f};
        default:       return {0.80f, 0.80f, 0.80f, 1.0f};
    }
}

std::string_view SolarSystemRenderer::planet_name(u32 planet_id)
{
    using namespace astro::planet_id;
    switch (planet_id)
    {
        case kMercury: return "Mercury";
        case kVenus:   return "Venus";
        case kMars:    return "Mars";
        case kJupiter: return "Jupiter";
        case kSaturn:  return "Saturn";
        case kUranus:  return "Uranus";
        case kNeptune: return "Neptune";
        default:       return "Unknown";
    }
}

f32 SolarSystemRenderer::magnitude_to_radius_ndc(f32 magnitude)
{
    // mag -5 → max icon; mag +3 → min icon; monotonic decreasing
    const f32 t = std::clamp((3.0f - magnitude) / 8.0f, 0.0f, 1.0f);
    return kMinIconRadiusNdc + t * (kMaxIconRadiusNdc - kMinIconRadiusNdc);
}

} // namespace parallax::rendering
