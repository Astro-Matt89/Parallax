#pragma once

/// @file visibility_filter.hpp
/// @brief Fast visibility prefilter for large star catalogs.
///
/// Reduces per-frame coordinate transforms from 118k to ~30-50k by:
///   1. Never-rises check: skip stars whose max altitude is always negative
///   2. Hour-angle check: skip stars currently below the horizon
///
/// All methods are static, const, and pure — no side effects.

#include "astro/coordinates.hpp"
#include "catalog/star_entry.hpp"
#include "core/types.hpp"

#include <cmath>
#include <span>
#include <vector>

namespace parallax::catalog
{
    /// @brief Statistics from the visibility prefilter pass.
    struct PrefilterStats
    {
        u32 total;              ///< Total stars in catalog
        u32 passed;             ///< Stars that passed the filter (need full transform)
        u32 skipped_never_rises; ///< Stars that never rise at this latitude
        u32 skipped_below_hz;   ///< Stars currently below horizon (hour angle check)
        u32 skipped_mag;        ///< Stars skipped by early magnitude cull
    };

    /// @brief Fast visibility prefilter to reduce per-frame coordinate transforms.
    ///
    /// For 118k stars, transforming all per frame costs ~24ms (too slow for 60fps).
    /// This prefilter uses cheap spherical geometry checks to skip stars that
    /// are definitely invisible, reducing the working set to ~30-50% of the catalog.
    ///
    /// The filter is conservative — it may pass stars that are actually off-screen,
    /// but it will never skip stars that are actually visible.
    class VisibilityFilter
    {
    public:
        VisibilityFilter() = delete;

        /// @brief Filter catalog to only potentially-visible stars.
        ///
        /// Returns indices into the input catalog for stars that pass the prefilter.
        /// Caller then runs the full transform pipeline only on these stars.
        ///
        /// @param stars Full star catalog.
        /// @param observer Observer location (latitude used for horizon geometry).
        /// @param lst Local sidereal time in radians.
        /// @param mag_limit Faintest magnitude to consider.
        /// @param[out] stats Optional statistics output.
        /// @return Indices of stars that may be visible.
        [[nodiscard]] static std::vector<u32> filter(
            std::span<const StarEntry> stars,
            const astro::ObserverLocation& observer,
            f64 lst,
            f32 mag_limit,
            PrefilterStats* stats = nullptr);

    private:
        /// @brief Check if a star at given declination ever rises above the horizon.
        ///
        /// A star's maximum altitude is: alt_max = 90° - |latitude - declination|
        /// If alt_max < 0, the star is circumpolar-invisible (never rises).
        ///
        /// We use a small margin (-1°) to account for refraction near horizon.
        ///
        /// @param dec_rad Star declination in radians.
        /// @param lat_rad Observer latitude in radians.
        /// @return true if the star can potentially rise above the horizon.
        [[nodiscard]] static bool can_rise(f64 dec_rad, f64 lat_rad);

        /// @brief Check if a star is currently above the horizon using hour angle.
        ///
        /// For a star at declination δ, observer latitude φ:
        ///   cos(H_set) = -tan(φ) × tan(δ)
        ///   If |cos(H_set)| > 1 → star is circumpolar (always up or always down)
        ///   Otherwise, star is above horizon if |hour_angle| < H_set
        ///
        /// We add a margin of 15° to H_set to be conservative (don't miss stars
        /// near the rising/setting boundary that might be in the FOV).
        ///
        /// @param ra_rad Star right ascension in radians.
        /// @param dec_rad Star declination in radians.
        /// @param lat_rad Observer latitude in radians.
        /// @param lst Local sidereal time in radians.
        /// @return true if the star is potentially above the horizon.
        [[nodiscard]] static bool is_above_horizon(
            f64 ra_rad, f64 dec_rad, f64 lat_rad, f64 lst);
    };

} // namespace parallax::catalog