/// @file camera.cpp
/// @brief Implementation of the observer camera system.

#include "rendering/camera.hpp"

#include "core/types.hpp"

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace parallax::rendering
{

Camera::Camera()
    : m_altitude(kDefaultAltitude)
    , m_azimuth(kDefaultAzimuth)
    , m_fov(kDefaultFov)
    , m_magnitude_limit(kDefaultMagLimit)
{
}

void Camera::set_pointing(f64 altitude_rad, f64 azimuth_rad)
{
    m_altitude = altitude_rad;
    m_azimuth = azimuth_rad;

    clamp_altitude();
    normalize_azimuth();
}

void Camera::set_fov(f64 fov_deg)
{
    m_fov = glm::radians(fov_deg);
    clamp_fov();
}

void Camera::pan(f64 delta_az_rad, f64 delta_alt_rad)
{
    m_azimuth += delta_az_rad;
    m_altitude += delta_alt_rad;

    clamp_altitude();
    normalize_azimuth();
}

void Camera::zoom(f64 factor)
{
    m_fov *= factor;
    clamp_fov();
}

void Camera::reset()
{
    m_altitude = kDefaultAltitude;
    m_azimuth = kDefaultAzimuth;
    m_fov = kDefaultFov;
    m_magnitude_limit = kDefaultMagLimit;
}

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

f32 Camera::get_magnitude_limit() const
{
    return m_magnitude_limit;
}

void Camera::set_magnitude_limit(f32 mag)
{
    m_magnitude_limit = std::clamp(mag, kMinMagLimit, kMaxMagLimit);
}

void Camera::adjust_magnitude_limit(f32 delta)
{
    set_magnitude_limit(m_magnitude_limit + delta);
}

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