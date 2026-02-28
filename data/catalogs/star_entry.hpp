#pragma once

/// @file star_entry.hpp
/// @brief Runtime star data structure for catalog entries.

#include "core/types.hpp"

namespace parallax::catalog
{
    /// @brief Runtime representation of a single star from the catalog.
    ///
    /// Expanded format for ease of use during rendering and computation.
    /// Coordinates are stored in radians (J2000 epoch).
    struct StarEntry
    {
        f64 ra;             ///< Right ascension (radians, 0..2π)
        f64 dec;            ///< Declination (radians, -π/2..+π/2)
        f32 mag_v;          ///< Visual magnitude (V-band)
        f32 color_bv;       ///< B-V color index
        u32 catalog_id;     ///< Source catalog ID (e.g., HIP number, or line index)
    };

} // namespace parallax::catalog