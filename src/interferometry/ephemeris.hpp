#pragma once

#include "core/types.hpp"

#include <string>

namespace parallax::interferometry
{
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
