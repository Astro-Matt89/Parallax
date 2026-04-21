#pragma once

/// @file camera.hpp
/// @brief Camera system: pointing direction, field of view, pan/zoom, magnitude limit.
///
/// SKYCHART MODE: Magnitude limit is a user-controlled slider, NOT
/// derived from FOV. The [ ] keys adjust it in 0.5 steps.

#include "astro/coordinates.hpp"
#include "core/types.hpp"

#include <glm/trigonometric.hpp>

namespace parallax::rendering
{
    /// @brief Observer camera that defines where the user is looking and the field of view.
    ///
    /// Stores pointing direction as horizontal coordinates (altitude/azimuth)
    /// and a symmetric field of view. Provides pan (mouse drag) and zoom (scroll)
    /// with appropriate clamping.
    ///
    /// Magnitude limit is an independent user setting, NOT coupled to FOV.
    class Camera
    {
    public:
        /// @brief Construct camera with default pointing: 45° up, due north, 60° FOV.
        Camera();

        /// @brief Set absolute pointing direction (Alt/Az).
        void set_pointing(f64 altitude_rad, f64 azimuth_rad);

        /// @brief Set field of view in degrees.
        void set_fov(f64 fov_deg);

        /// @brief Adjust pointing by a delta (for mouse drag).
        void pan(f64 delta_az_rad, f64 delta_alt_rad);

        /// @brief Zoom in or out by multiplying the FOV.
        void zoom(f64 factor);

        /// @brief Reset camera to default: 45° up, due north, 60° FOV, 6.5 MLIM.
        void reset();

        [[nodiscard]] astro::HorizontalCoord get_pointing() const;
        [[nodiscard]] f64 get_fov_rad() const;
        [[nodiscard]] f64 get_fov_deg() const;

        /// @brief Get the current user-controlled magnitude limit.
        [[nodiscard]] f32 get_magnitude_limit() const;

        /// @brief Set the magnitude limit directly.
        /// @param mag Clamped to [kMinMagLimit, kMaxMagLimit].
        void set_magnitude_limit(f32 mag);

        /// @brief Adjust magnitude limit by delta (e.g. +0.5 or -0.5).
        void adjust_magnitude_limit(f32 delta);

    private:
        void clamp_altitude();
        void normalize_azimuth();
        void clamp_fov();

        f64 m_altitude;     ///< Current altitude (radians)
        f64 m_azimuth;      ///< Current azimuth (radians)
        f64 m_fov;          ///< Current field of view (radians)
        f32 m_magnitude_limit;  ///< User-controlled magnitude limit

        // FOV limits
        static constexpr f64 kMinFovDeg = 0.5;
        static constexpr f64 kMaxFovDeg = 120.0;
        static constexpr f64 kMinFov = glm::radians(kMinFovDeg);
        static constexpr f64 kMaxFov = glm::radians(kMaxFovDeg);

        // Defaults
        static constexpr f64 kDefaultAltitude = glm::radians(45.0);
        static constexpr f64 kDefaultAzimuth  = 0.0;
        static constexpr f64 kDefaultFov      = glm::radians(60.0);
        static constexpr f32 kDefaultMagLimit = 6.5f;

        // Magnitude limit range
        static constexpr f32 kMinMagLimit = 0.0f;   ///< Only the very brightest
        static constexpr f32 kMaxMagLimit = 20.0f;   ///< Full catalog depth
    };

} // namespace parallax::rendering