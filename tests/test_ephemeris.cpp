/// @file test_ephemeris.cpp
/// @brief Unit tests for interferometric ephemerides (Sprint 10b Task 10b.1).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "interferometry/ephemeris.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    using parallax::Vec3d;
    using parallax::astro_constants::kDegToRad;
    using parallax::astro_constants::kPi;
    using parallax::interferometry::Body;
    using parallax::interferometry::Station;
    using parallax::interferometry::StationState;

    constexpr double kOmegaE = 15.0 * kPi / 180.0;
    constexpr double kOmegaM = (360.0 / (27.321661 * 24.0)) * kPi / 180.0;

    constexpr double kDMoon = 384400e3;
    constexpr double kRMoon = 1737.4e3;
    constexpr double kREarth = 6371e3;
    constexpr double kIncMoon = 20.0 * kDegToRad;
    constexpr double kMoonPhase0 = 70.0 * kDegToRad;
    constexpr double kElMin = 10.0 * kDegToRad;

    constexpr double kTychoLat = -43.3 * kDegToRad;
    constexpr double kTychoLon = -11.2 * kDegToRad;

    constexpr double kRelTol = 1.0e-9;

    [[nodiscard]] bool approx_rel(double actual, double expected, double rel_tol = kRelTol)
    {
        const double scale = std::max(std::abs(expected), 1.0);
        return std::abs(actual - expected) <= rel_tol * scale;
    }

    [[nodiscard]] Vec3d rotate_z(const Vec3d& v, double angle)
    {
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        return Vec3d {
            c * v.x - s * v.y,
            s * v.x + c * v.y,
            v.z,
        };
    }

    [[nodiscard]] Vec3d make_perpendicular(const Vec3d& v)
    {
        const Vec3d axis = (std::abs(v.z) < 0.9) ? Vec3d {0.0, 0.0, 1.0} : Vec3d {1.0, 0.0, 0.0};
        return glm::normalize(glm::cross(v, axis));
    }
}

TEST_CASE("moon_center_at(0) matches sandbox formula and keeps |Cm| = D_MOON")
{
    const Vec3d cm = parallax::interferometry::moon_center_at(0.0);

    const double a = kMoonPhase0;
    const Vec3d expected {
        kDMoon * std::cos(a),
        kDMoon * std::sin(a) * std::cos(kIncMoon),
        kDMoon * std::sin(a) * std::sin(kIncMoon),
    };

    CHECK(approx_rel(cm.x, expected.x));
    CHECK(approx_rel(cm.y, expected.y));
    CHECK(approx_rel(cm.z, expected.z));
    CHECK(approx_rel(glm::length(cm), kDMoon));
}

TEST_CASE("moon_center_at(6) advances by OMEGA_M*6 and keeps |Cm| = D_MOON")
{
    const Vec3d cm = parallax::interferometry::moon_center_at(6.0);

    const double a = kMoonPhase0 + kOmegaM * 6.0;
    const Vec3d expected {
        kDMoon * std::cos(a),
        kDMoon * std::sin(a) * std::cos(kIncMoon),
        kDMoon * std::sin(a) * std::sin(kIncMoon),
    };

    CHECK(approx_rel(cm.x, expected.x));
    CHECK(approx_rel(cm.y, expected.y));
    CHECK(approx_rel(cm.z, expected.z));
    CHECK(approx_rel(glm::length(cm), kDMoon));
}

TEST_CASE("Earth station state preserves radius, up normalization, and z-axis rotation")
{
    const Station la_palma {
        .name = "La Palma",
        .body = Body::Earth,
        .lat = 28.7569 * kDegToRad,
        .lon = -17.8925 * kDegToRad,
    };

    const StationState st0 = parallax::interferometry::station_state(la_palma, 0.0);
    const StationState st6 = parallax::interferometry::station_state(la_palma, 6.0);

    CHECK(approx_rel(glm::length(st0.position), kREarth));
    CHECK(approx_rel(glm::length(st6.position), kREarth));
    CHECK(approx_rel(glm::length(st0.up), 1.0));
    CHECK(approx_rel(glm::length(st6.up), 1.0));

    const Vec3d expected_up6 = rotate_z(st0.up, kOmegaE * 6.0);
    CHECK(approx_rel(st6.up.x, expected_up6.x));
    CHECK(approx_rel(st6.up.y, expected_up6.y));
    CHECK(approx_rel(st6.up.z, expected_up6.z));
}

TEST_CASE("Moon station state preserves lunar radius and sub-Earth point orientation")
{
    const Station tycho {
        .name = "Tycho",
        .body = Body::Moon,
        .lat = kTychoLat,
        .lon = kTychoLon,
    };

    const StationState st0 = parallax::interferometry::station_state(tycho, 0.0);
    const StationState st6 = parallax::interferometry::station_state(tycho, 6.0);

    const Vec3d cm0 = parallax::interferometry::moon_center_at(0.0);
    const Vec3d cm6 = parallax::interferometry::moon_center_at(6.0);

    CHECK(approx_rel(glm::length(st0.position - cm0), kRMoon));
    CHECK(approx_rel(glm::length(st6.position - cm6), kRMoon));
    CHECK(approx_rel(glm::length(st0.up), 1.0));
    CHECK(approx_rel(glm::length(st6.up), 1.0));

    const Station sub_earth {
        .name = "SubEarth",
        .body = Body::Moon,
        .lat = 0.0,
        .lon = 0.0,
    };

    const StationState sub_earth_st = parallax::interferometry::station_state(sub_earth, 0.0);
    const Vec3d to_earth_dir = -glm::normalize(cm0);
    CHECK(approx_rel(glm::dot(sub_earth_st.up, to_earth_dir), 1.0));
}

TEST_CASE("occulted_by handles behind-body, tangent, and proj<=0 cases")
{
    const Station la_palma {
        .name = "La Palma",
        .body = Body::Earth,
        .lat = 28.7569 * kDegToRad,
        .lon = -17.8925 * kDegToRad,
    };

    const StationState st = parallax::interferometry::station_state(la_palma, 0.0);
    const Vec3d moon_center = parallax::interferometry::moon_center_at(0.0);
    const Vec3d to_moon = moon_center - st.position;

    const Vec3d source_behind = glm::normalize(to_moon);
    CHECK(parallax::interferometry::occulted_by(st.position, source_behind, moon_center, kRMoon));

    const Vec3d source_ninety = make_perpendicular(to_moon);
    CHECK_FALSE(parallax::interferometry::occulted_by(st.position, source_ninety, moon_center, kRMoon));

    const Vec3d source_opposite = -source_behind;
    CHECK_FALSE(parallax::interferometry::occulted_by(st.position, source_opposite, moon_center, kRMoon));
}

TEST_CASE("is_visible enforces EL_MIN horizon threshold")
{
    const StationState st {
        .position = Vec3d {0.0, 0.0, 0.0},
        .up = Vec3d {0.0, 0.0, 1.0},
    };

    const double eps = 1.0e-6;

    const Vec3d source_above {
        std::cos(kElMin + eps),
        0.0,
        std::sin(kElMin + eps),
    };

    const Vec3d source_below {
        std::cos(kElMin - eps),
        0.0,
        std::sin(kElMin - eps),
    };

    CHECK(parallax::interferometry::is_visible(st, source_above, 0.0, Body::Earth));
    CHECK_FALSE(parallax::interferometry::is_visible(st, source_below, 0.0, Body::Earth));
}
