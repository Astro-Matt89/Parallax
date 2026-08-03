#pragma once

#include "core/types.hpp"

#include <string>

namespace parallax::interferometry
{
    inline constexpr double kOmegaE = 15.0 * astro_constants::kPi / 180.0;
    inline constexpr double kOmegaM = (360.0 / (27.321661 * 24.0)) * astro_constants::kPi / 180.0;

    inline constexpr double kDMoon = 384400e3;
    inline constexpr double kRMoon = 1737.4e3;
    inline constexpr double kREarth = 6371e3;
    inline constexpr double kIncMoon = 20.0 * astro_constants::kDegToRad;

    inline constexpr double kTychoLat = -43.3 * astro_constants::kDegToRad;
    inline constexpr double kTychoLon = -11.2 * astro_constants::kDegToRad;

    inline constexpr double kMoonPhase0 = 70.0 * astro_constants::kDegToRad;
    inline constexpr double kElMin = 10.0 * astro_constants::kDegToRad;

    enum class Body
    {
        Earth,
        Moon,
    };

    struct Station
    {
        std::string name;
        Body body;
        double lat; // radians
        double lon; // radians
    };

    struct StationState
    {
        Vec3d position; // metres
        Vec3d up;       // unit vector
    };

    [[nodiscard]] Vec3d moon_center_at(double time_hours);

    [[nodiscard]] StationState station_state(const Station& station, double time_hours);

    [[nodiscard]] bool occulted_by(
        const Vec3d& station_position,
        const Vec3d& source_dir,
        const Vec3d& body_center,
        double body_radius);

    [[nodiscard]] bool is_visible(
        const StationState& station_state,
        const Vec3d& source_dir,
        double time_hours,
        Body station_body);
}
