#pragma once

/// @file camera.hpp
/// @brief Camera system: pointing direction, field of view, pan/zoom.

#include "astro/coordinates.hpp"
#include "core/types.hpp"

#include <glm/trigonometric.hpp>

namespace parallax::rendering
{
    /// @brief Observer camera that defines where the user is looking and the field of view.
    ///
    /// Stores pointing direction as horizontal coordinates (altitude/azimuth)
    /// and a symmetric field of view. Provides pan (mouse drag) and zoom (scroll)
    /// with appropriate clamping. Computes a magnitude limit heuristic based on FOV.
    class Camera
    {
    public:
        /// @brief Construct camera with default pointing: 45° up, due north, 60° FOV.
        Camera();

        /// @brief Set absolute pointing direction (Alt/Az).
        /// @param altitude_rad Altitude in radians, clamped to [-π/2, π/2].
        /// @param azimuth_rad Azimuth in radians, normalized to [0, 2π).
        void set_pointing(f64 altitude_rad, f64 azimuth_rad);

        /// @brief Set field of view in degrees.
        /// @param fov_deg FOV in degrees, clamped to [kMinFovDeg, kMaxFovDeg].
        void set_fov(f64 fov_deg);

        /// @brief Adjust pointing by a delta (for mouse drag).
        /// @param delta_az_rad Azimuth offset in radians (positive = pan right/east).
        /// @param delta_alt_rad Altitude offset in radians (positive = pan up).
        void pan(f64 delta_az_rad, f64 delta_alt_rad);

        /// @brief Zoom in or out by multiplying the FOV.
        /// @param factor Zoom factor. <1.0 zooms in, >1.0 zooms out.
        void zoom(f64 factor);

        /// @brief Reset camera to default: 45° up, due north, 60° FOV.
        void reset();

        /// @brief Get the current pointing direction as a HorizontalCoord.
        [[nodiscard]] astro::HorizontalCoord get_pointing() const;

        /// @brief Get the current field of view in radians.
        [[nodiscard]] f64 get_fov_rad() const;

        /// @brief Get the current field of view in degrees.
        [[nodiscard]] f64 get_fov_deg() const;

        /// @brief Get the limiting magnitude for the current FOV.
        ///
        /// Uses the heuristic: mag_limit = 6.5 + 5 × log10(60.0 / fov_degrees).
        /// At 60° FOV (naked eye): ~6.5
        /// At 5° FOV (binoculars): ~10
        /// At 0.5° FOV (telescope): ~14
        [[nodiscard]] f32 get_magnitude_limit() const;

    private:
        /// @brief Clamp altitude to [-π/2, π/2].
        void clamp_altitude();

        /// @brief Normalize azimuth to [0, 2π).
        void normalize_azimuth();

        /// @brief Clamp FOV to [kMinFov, kMaxFov].
        void clamp_fov();

        f64 m_altitude;     ///< Current altitude (radians)
        f64 m_azimuth;      ///< Current azimuth (radians)
        f64 m_fov;          ///< Current field of view (radians)

        // -----------------------------------------------------------------
        // FOV limits
        // -----------------------------------------------------------------
        static constexpr f64 kMinFovDeg = 0.5;     ///< Maximum zoom in (telescope)
        static constexpr f64 kMaxFovDeg = 120.0;   ///< Maximum zoom out (ultra-wide)
        static constexpr f64 kMinFov = glm::radians(kMinFovDeg);
        static constexpr f64 kMaxFov = glm::radians(kMaxFovDeg);

        // -----------------------------------------------------------------
        // Default values
        // -----------------------------------------------------------------
        static constexpr f64 kDefaultAltitude = glm::radians(45.0);    ///< 45° up
        static constexpr f64 kDefaultAzimuth  = 0.0;                   ///< Due north
        static constexpr f64 kDefaultFov      = glm::radians(60.0);    ///< 60° naked eye

        // -----------------------------------------------------------------
        // Magnitude limit constants
        // -----------------------------------------------------------------
        static constexpr f64 kBaseMagLimit    = 6.5;    ///< Naked-eye limit at 60° FOV
        static constexpr f64 kReferenceFovDeg = 60.0;   ///< FOV for base magnitude limit
        static constexpr f32 kMaxMagLimit     = 20.0f;  ///< Absolute upper clamp
    };

} // namespace parallax::rendering