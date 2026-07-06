#pragma once

/// @file spectral_band.hpp
/// @brief SpectralBand — a wavelength band the array instrument can observe in.

#include "core/types.hpp"

#include <string>

namespace parallax::instruments
{
    /// @brief A spectral band (wavelength window) the instrument can observe in.
    ///
    /// Progression unlocks additional bands over time (Visible/Near-IR at start,
    /// then Mid-IR / Radio-K / Submm). Locked bands are displayed but cannot be
    /// activated for an observation.
    struct SpectralBand
    {
        std::string name{};                 ///< Display name, e.g. "Visible", "Radio-K".
        f64 center_wavelength_nm = 0.0;     ///< Band centre wavelength (nanometres).
        f64 bandwidth_nm = 0.0;             ///< Full band width (nanometres).
        bool is_unlocked = false;           ///< Whether the player may use this band yet.
        bool is_active = false;             ///< Whether this band is selected for observation.
    };
}
