#pragma once

/// @file station.hpp
/// @brief Station — a single collecting element of an array instrument.

#include "astro/observer.hpp"
#include "core/types.hpp"

#include <string>

namespace parallax::instruments
{
    /// @brief A single collecting element (dish/telescope) of the array.
    ///
    /// In total-power mode (Sprint 10a) the active stations sum their collecting
    /// areas into one effective aperture for SNR purposes. Baselines between
    /// stations are unused until Sprint 10b (aperture synthesis).
    struct Station
    {
        std::string name{};                     ///< Display name, e.g. "Tycho Primary".
        astro::ParentBody body{astro::ParentBody::Earth}; ///< Host body (Moon, Earth, ...).
        f64 latitude_rad = 0.0;                 ///< Geographic latitude (radians, north positive).
        f64 longitude_rad = 0.0;                ///< Geographic longitude (radians, east positive).
        f64 elevation_m = 0.0;                  ///< Elevation above reference surface (metres).

        f32 aperture_diameter_m = 0.0f;         ///< Collecting aperture diameter (metres).
        f32 efficiency = 1.0f;                  ///< Combined optical + quantum efficiency (0..1).
        bool has_atmosphere = false;            ///< True for Earth stations (adds system noise).
        bool is_active = true;                  ///< Player-toggleable participation in observations.
    };
}
