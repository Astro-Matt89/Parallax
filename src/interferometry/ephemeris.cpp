#include "interferometry/ephemeris.hpp"

#include "core/types.hpp"

#include <cmath>

namespace parallax::interferometry
{
    namespace
    {
        using astro_constants::kDegToRad;
        using astro_constants::kPi;

        // Normative Sprint 10b constants (ported from glasswing-sandbox-v1_7).
        constexpr double kOmegaE = 15.0 * kPi / 180.0;
        constexpr double kOmegaM = (360.0 / (27.321661 * 24.0)) * kPi / 180.0;

        constexpr double kDMoon = 384400e3;
        constexpr double kRMoon = 1737.4e3;
        constexpr double kREarth = 6371e3;
        constexpr double kIncMoon = 20.0 * kDegToRad;

        // NOTE: Sprint 10b ephemeris fixtures use the sandbox oracle values
        // TYCHO = { lat: -43.3 deg, lon: -11.2 deg } (not the base-site values).
        [[maybe_unused]] constexpr double kTychoLat = -43.3 * kDegToRad;
        [[maybe_unused]] constexpr double kTychoLon = -11.2 * kDegToRad;

        constexpr double kMoonPhase0 = 70.0 * kDegToRad;
        constexpr double kElMin = 10.0 * kDegToRad;

        [[nodiscard]] Vec3d moon_orbit_normal()
        {
            return Vec3d {
                0.0,
                -std::sin(kIncMoon),
                std::cos(kIncMoon),
            };
        }
    }

    Vec3d moon_center_at(double time_hours)
    {
        const double a = kMoonPhase0 + kOmegaM * time_hours;

        return Vec3d {
            kDMoon * std::cos(a),
            kDMoon * std::sin(a) * std::cos(kIncMoon),
            kDMoon * std::sin(a) * std::sin(kIncMoon),
        };
    }

    StationState station_state(const Station& station, double time_hours)
    {
        if (station.body == Body::Earth)
        {
            const double theta = station.lon + kOmegaE * time_hours;

            const Vec3d up {
                std::cos(station.lat) * std::cos(theta),
                std::cos(station.lat) * std::sin(theta),
                std::sin(station.lat),
            };

            return StationState {
                .position = kREarth * up,
                .up = up,
            };
        }

        const Vec3d Cm = moon_center_at(time_hours);
        const double Dm = glm::length(Cm);
        const Vec3d e = -Cm / Dm;
        const Vec3d kv = moon_orbit_normal();
        const Vec3d mv = glm::cross(kv, e);

        const Vec3d up = std::cos(station.lat) * (std::cos(station.lon) * e + std::sin(station.lon) * mv)
            + std::sin(station.lat) * kv;

        return StationState {
            .position = Cm + kRMoon * up,
            .up = up,
        };
    }

    bool occulted_by(
        const Vec3d& station_position,
        const Vec3d& source_dir,
        const Vec3d& body_center,
        double body_radius)
    {
        const Vec3d d = body_center - station_position;
        const double proj = glm::dot(d, source_dir);
        if (proj <= 0.0)
        {
            return false;
        }

        const double d2 = glm::dot(d, d) - proj * proj;
        return d2 < body_radius * body_radius;
    }

    bool is_visible(
        const StationState& station_state,
        const Vec3d& source_dir,
        double time_hours,
        Body station_body)
    {
        if (glm::dot(station_state.up, source_dir) < std::sin(kElMin))
        {
            return false;
        }

        if (station_body == Body::Earth)
        {
            return !occulted_by(
                station_state.position,
                source_dir,
                moon_center_at(time_hours),
                kRMoon);
        }

        return !occulted_by(
            station_state.position,
            source_dir,
            Vec3d {0.0, 0.0, 0.0},
            kREarth);
    }
}
