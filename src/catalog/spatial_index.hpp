#pragma once

/// @file spatial_index.hpp
/// @brief Declination-band spatial index for fast FOV queries on star catalogs.
///
/// Divides the sky into 180 bands of 1° declination each (-90° to +90°).
/// Stars within each band are sorted by right ascension for binary search.
///
/// Query algorithm:
///   1. Determine which Dec bands overlap with the FOV disc.
///   2. For each overlapping band, binary-search the RA range.
///   3. Apply magnitude limit during scan.
///   4. Return indices into the original star vector.
///
/// Complexity: O(bands_in_fov × log(stars_per_band) + result_count)
/// Performance: < 5ms for FOV 60°, MLIM 10, 2.5M stars on modern CPU.
///
/// This is a Sprint 04 interim index. HEALPix indexing comes with
/// the binary .plxcat format in a later sprint.

#include "catalog/star_entry.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace parallax::catalog
{
    /// @brief Statistics from a spatial index query.
    struct SpatialQueryStats
    {
        u32 bands_searched;     ///< Number of declination bands examined
        u32 candidates_scanned; ///< Stars examined within matching bands
        u32 results;            ///< Stars that passed all filters
        f64 query_time_ms;      ///< Wall-clock time for the query
    };

    /// @brief Declination-band spatial index for star catalogs.
    ///
    /// Build once after loading a catalog, then query per frame.
    /// The index stores indices into the original star vector — it does
    /// not copy star data.
    class SpatialIndex
    {
    public:
        SpatialIndex() = default;
        ~SpatialIndex() = default;

        SpatialIndex(const SpatialIndex&) = delete;
        SpatialIndex& operator=(const SpatialIndex&) = delete;
        SpatialIndex(SpatialIndex&&) = default;
        SpatialIndex& operator=(SpatialIndex&&) = default;

        /// @brief Build the index from a loaded star catalog.
        ///
        /// Assigns each star to a declination band and sorts by RA within
        /// each band. The stars span must remain valid for the index lifetime.
        ///
        /// @param stars  Full star catalog (must outlive the index).
        /// @param num_bands Number of declination bands (default 180 = 1° each).
        void build(std::span<const StarEntry> stars, u32 num_bands = 180);

        /// @brief Query all stars within a sky disc that pass the magnitude limit.
        ///
        /// @param ra_center  Disc center RA (radians).
        /// @param dec_center Disc center Dec (radians).
        /// @param radius_rad Disc radius (radians).
        /// @param mag_limit  Faintest magnitude to include.
        /// @param[out] stats Optional query statistics.
        /// @return Indices into the original star vector.
        [[nodiscard]] std::vector<u32> query(
            f64 ra_center,
            f64 dec_center,
            f64 radius_rad,
            f32 mag_limit,
            SpatialQueryStats* stats = nullptr) const;

        /// @brief Total number of stars indexed.
        [[nodiscard]] u32 get_star_count() const;

        /// @brief Whether the index has been built.
        [[nodiscard]] bool is_built() const;

    private:
        /// @brief A single declination band containing star indices sorted by RA.
        struct Band
        {
            f64 dec_min;    ///< Lower declination bound (radians)
            f64 dec_max;    ///< Upper declination bound (radians)

            /// @brief Star indices sorted by RA within this band.
            /// Each entry is a (ra, original_index) pair for binary search.
            std::vector<std::pair<f64, u32>> entries;
        };

        /// @brief Determine band index for a given declination.
        [[nodiscard]] u32 dec_to_band(f64 dec_rad) const;

        /// @brief Compute the RA range visible at a given Dec offset from disc center.
        ///
        /// At declinations near the poles, the RA range for a given angular radius
        /// expands (wider in RA to cover the same angular distance). This computes
        /// the half-width in RA needed to cover the disc at a given Dec.
        [[nodiscard]] static f64 ra_half_width(f64 dec_rad, f64 radius_rad);

        std::vector<Band> m_bands;
        std::span<const StarEntry> m_stars;
        u32 m_num_bands = 0;
        f64 m_band_width = 0.0;     ///< Width of each band (radians)
        bool m_built = false;
    };

} // namespace parallax::catalog