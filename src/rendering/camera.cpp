/// @file camera.cpp
/// @brief Implementation of the observer camera system.

#include "rendering/camera.hpp"

#include "core/types.hpp"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace parallax::rendering
{

// -----------------------------------------------------------------
// Construction
// -----------------------------------------------------------------

Camera::Camera()
    : m_altitude(kDefaultAltitude)
    , m_azimuth(kDefaultAzimuth)
    , m_fov(kDefaultFov)
{
}

// -----------------------------------------------------------------
// set_pointing — absolute Alt/Az
// -----------------------------------------------------------------

void Camera::set_pointing(f64 altitude_rad, f64 azimuth_rad)
{
    m_altitude = altitude_rad;
    m_azimuth = azimuth_rad;

    clamp_altitude();
    normalize_azimuth();
}

// -----------------------------------------------------------------
// set_fov — in degrees, converted to radians
// -----------------------------------------------------------------

void Camera::set_fov(f64 fov_deg)
{
    m_fov = glm::radians(fov_deg);
    clamp_fov();
}

// -----------------------------------------------------------------
// pan — delta adjustment for mouse drag
// -----------------------------------------------------------------

void Camera::pan(f64 delta_az_rad, f64 delta_alt_rad)
{
    m_azimuth += delta_az_rad;
    m_altitude += delta_alt_rad;

    clamp_altitude();
    normalize_azimuth();
}

// -----------------------------------------------------------------
// zoom — multiply FOV by factor
// -----------------------------------------------------------------

void Camera::zoom(f64 factor)
{
    m_fov *= factor;
    clamp_fov();
}

// -----------------------------------------------------------------
// reset — back to defaults
// -----------------------------------------------------------------

void Camera::reset()
{
    m_altitude = kDefaultAltitude;
    m_azimuth = kDefaultAzimuth;
    m_fov = kDefaultFov;
}

// -----------------------------------------------------------------
// Getters
// -----------------------------------------------------------------

astro::HorizontalCoord Camera::get_pointing() const
{
    return astro::HorizontalCoord{
        .alt = m_altitude,
        .az  = m_azimuth,
    };
}

f64 Camera::get_fov_rad() const
{
    return m_fov;
}

f64 Camera::get_fov_deg() const
{
    return glm::degrees(m_fov);
}

// -----------------------------------------------------------------
// Magnitude limit heuristic
//
// mag_limit = 6.5 + 5 × log10(60.0 / fov_degrees)
//
// This models the increase in limiting magnitude as the FOV narrows
// (i.e., zooming in with optics concentrates more light per pixel).
// -----------------------------------------------------------------

f32 Camera::get_magnitude_limit() const
{
    const f64 fov_deg = glm::degrees(m_fov);

    const f64 mag_limit = kBaseMagLimit
                        + 5.0 * std::log10(kReferenceFovDeg / fov_deg);

    return static_cast<f32>(std::min(mag_limit, static_cast<f64>(kMaxMagLimit)));
}

// -----------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------

void Camera::clamp_altitude()
{
    m_altitude = std::clamp(m_altitude,
                            -astro_constants::kHalfPi,
                             astro_constants::kHalfPi);
}

void Camera::normalize_azimuth()
{
    m_azimuth = std::fmod(m_azimuth, astro_constants::kTwoPi);
    if (m_azimuth < 0.0)
    {
        m_azimuth += astro_constants::kTwoPi;
    }
}

void Camera::clamp_fov()
{
    m_fov = std::clamp(m_fov, kMinFov, kMaxFov);
}

} // namespace parallax::rendering