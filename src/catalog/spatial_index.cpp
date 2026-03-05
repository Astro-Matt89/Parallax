/// @file spatial_index.cpp
/// @brief Declination-band spatial index implementation.
///
/// Build: O(N log N) — sort stars by RA within each band.
/// Query: O(B × log S + R) where B = bands in FOV, S = stars per band,
///        R = result count.

#include "catalog/spatial_index.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace parallax::catalog
{

// =================================================================
// Build
// =================================================================

void SpatialIndex::build(std::span<const StarEntry> stars, u32 num_bands)
{
    m_stars = stars;
    m_num_bands = num_bands;
    m_band_width = astro_constants::kPi / static_cast<f64>(num_bands);

    m_bands.clear();
    m_bands.resize(num_bands);

    // Initialize band boundaries
    for (u32 b = 0; b < num_bands; ++b)
    {
        m_bands[b].dec_min = -astro_constants::kHalfPi +
            static_cast<f64>(b) * m_band_width;
        m_bands[b].dec_max = m_bands[b].dec_min + m_band_width;
    }

    // Assign each star to its band
    for (u32 i = 0; i < static_cast<u32>(stars.size()); ++i)
    {
        const u32 band = dec_to_band(stars[i].dec);
        m_bands[band].entries.emplace_back(stars[i].ra, i);
    }

    // Sort each band by RA for binary search
    for (auto& band : m_bands)
    {
        std::sort(band.entries.begin(), band.entries.end(),
            [](const auto& a, const auto& b)
            {
                return a.first < b.first;
            });
    }

    m_built = true;

    // Log band statistics
    u32 max_band = 0;
    u32 min_band = static_cast<u32>(stars.size());
    u32 empty_bands = 0;

    for (const auto& band : m_bands)
    {
        const auto count = static_cast<u32>(band.entries.size());
        if (count > max_band) { max_band = count; }
        if (count < min_band) { min_band = count; }
        if (count == 0) { ++empty_bands; }
    }

    PLX_CORE_INFO("SpatialIndex built: {} stars, {} bands ({} empty), "
                  "band size {}-{}",
                  stars.size(), num_bands, empty_bands, min_band, max_band);
}

// =================================================================
// Query
// =================================================================

std::vector<u32> SpatialIndex::query(
    f64 ra_center,
    f64 dec_center,
    f64 radius_rad,
    f32 mag_limit,
    SpatialQueryStats* stats) const
{
    using Clock = std::chrono::high_resolution_clock;
    const auto t_start = Clock::now();

    std::vector<u32> result;

    if (!m_built || m_stars.empty())
    {
        if (stats)
        {
            *stats = {};
        }
        return result;
    }

    // Pre-allocate for typical FOV result size
    result.reserve(16384);

    u32 bands_searched = 0;
    u32 candidates_scanned = 0;

    // Determine Dec range of the query disc
    const f64 dec_min = dec_center - radius_rad;
    const f64 dec_max = dec_center + radius_rad;

    // Clamp to valid Dec range and determine band indices
    const u32 band_lo = dec_to_band(std::max(dec_min, -astro_constants::kHalfPi));
    const u32 band_hi = dec_to_band(std::min(dec_max,
        astro_constants::kHalfPi - 1e-10));

    // Precompute for angular distance check
    const f64 cos_radius = std::cos(radius_rad);
    const f64 sin_dec_c = std::sin(dec_center);
    const f64 cos_dec_c = std::cos(dec_center);

    for (u32 b = band_lo; b <= band_hi; ++b)
    {
        const auto& band = m_bands[b];
        if (band.entries.empty())
        {
            continue;
        }

        ++bands_searched;

        // Compute RA half-width at this band's center declination
        const f64 band_dec_center = band.dec_min + m_band_width * 0.5;
        const f64 ra_hw = ra_half_width(band_dec_center, radius_rad);

        // If RA half-width >= π, we need to scan the entire band
        if (ra_hw >= astro_constants::kPi)
        {
            // Full band scan
            for (const auto& [star_ra, star_idx] : band.entries)
            {
                ++candidates_scanned;

                // Magnitude filter first (cheapest)
                if (m_stars[star_idx].mag_v > mag_limit)
                {
                    continue;
                }

                // Exact angular distance check
                const f64 sin_dec_s = std::sin(m_stars[star_idx].dec);
                const f64 cos_dec_s = std::cos(m_stars[star_idx].dec);
                const f64 cos_dra = std::cos(star_ra - ra_center);
                const f64 cos_dist = sin_dec_c * sin_dec_s +
                                     cos_dec_c * cos_dec_s * cos_dra;

                if (cos_dist >= cos_radius)
                {
                    result.push_back(star_idx);
                }
            }
            continue;
        }

        // RA range with wrapping at 0/2π
        f64 ra_lo = ra_center - ra_hw;
        f64 ra_hi = ra_center + ra_hw;

        const bool wraps = (ra_lo < 0.0 || ra_hi >= astro_constants::kTwoPi);

        if (!wraps)
        {
            // Simple case: contiguous RA range
            // Binary search for ra_lo
            auto it_lo = std::lower_bound(
                band.entries.begin(), band.entries.end(), ra_lo,
                [](const auto& entry, f64 val) { return entry.first < val; });

            for (auto it = it_lo; it != band.entries.end() && it->first <= ra_hi; ++it)
            {
                ++candidates_scanned;
                const u32 star_idx = it->second;

                if (m_stars[star_idx].mag_v > mag_limit)
                {
                    continue;
                }

                const f64 sin_dec_s = std::sin(m_stars[star_idx].dec);
                const f64 cos_dec_s = std::cos(m_stars[star_idx].dec);
                const f64 cos_dra = std::cos(it->first - ra_center);
                const f64 cos_dist = sin_dec_c * sin_dec_s +
                                     cos_dec_c * cos_dec_s * cos_dra;

                if (cos_dist >= cos_radius)
                {
                    result.push_back(star_idx);
                }
            }
        }
        else
        {
            // Wrapping case: query spans the 0/2π boundary
            // Normalize to [0, 2π)
            const f64 ra_lo_wrapped = ra_lo < 0.0 ?
                ra_lo + astro_constants::kTwoPi : ra_lo;
            const f64 ra_hi_wrapped = ra_hi >= astro_constants::kTwoPi ?
                ra_hi - astro_constants::kTwoPi : ra_hi;

            // Scan two ranges: [ra_lo_wrapped, 2π) and [0, ra_hi_wrapped]
            // Range 1: [ra_lo_wrapped, end)
            auto it1 = std::lower_bound(
                band.entries.begin(), band.entries.end(), ra_lo_wrapped,
                [](const auto& entry, f64 val) { return entry.first < val; });

            for (auto it = it1; it != band.entries.end(); ++it)
            {
                ++candidates_scanned;
                const u32 star_idx = it->second;

                if (m_stars[star_idx].mag_v > mag_limit)
                {
                    continue;
                }

                const f64 sin_dec_s = std::sin(m_stars[star_idx].dec);
                const f64 cos_dec_s = std::cos(m_stars[star_idx].dec);
                const f64 cos_dra = std::cos(it->first - ra_center);
                const f64 cos_dist = sin_dec_c * sin_dec_s +
                                     cos_dec_c * cos_dec_s * cos_dra;

                if (cos_dist >= cos_radius)
                {
                    result.push_back(star_idx);
                }
            }

            // Range 2: [0, ra_hi_wrapped]
            for (auto it = band.entries.begin();
                 it != band.entries.end() && it->first <= ra_hi_wrapped; ++it)
            {
                ++candidates_scanned;
                const u32 star_idx = it->second;

                if (m_stars[star_idx].mag_v > mag_limit)
                {
                    continue;
                }

                const f64 sin_dec_s = std::sin(m_stars[star_idx].dec);
                const f64 cos_dec_s = std::cos(m_stars[star_idx].dec);
                const f64 cos_dra = std::cos(it->first - ra_center);
                const f64 cos_dist = sin_dec_c * sin_dec_s +
                                     cos_dec_c * cos_dec_s * cos_dra;

                if (cos_dist >= cos_radius)
                {
                    result.push_back(star_idx);
                }
            }
        }
    }

    const auto t_end = Clock::now();
    const f64 elapsed_ms = std::chrono::duration<f64, std::milli>(t_end - t_start).count();

    if (stats)
    {
        *stats = SpatialQueryStats{
            .bands_searched = bands_searched,
            .candidates_scanned = candidates_scanned,
            .results = static_cast<u32>(result.size()),
            .query_time_ms = elapsed_ms,
        };
    }

    return result;
}

// =================================================================
// Helpers
// =================================================================

u32 SpatialIndex::get_star_count() const
{
    return static_cast<u32>(m_stars.size());
}

bool SpatialIndex::is_built() const
{
    return m_built;
}

u32 SpatialIndex::dec_to_band(f64 dec_rad) const
{
    // Map Dec from [-π/2, +π/2] to band index [0, m_num_bands - 1]
    const f64 normalized = (dec_rad + astro_constants::kHalfPi) / m_band_width;
    auto band = static_cast<i32>(normalized);

    // Clamp to valid range
    if (band < 0)
    {
        band = 0;
    }
    if (band >= static_cast<i32>(m_num_bands))
    {
        band = static_cast<i32>(m_num_bands) - 1;
    }

    return static_cast<u32>(band);
}

f64 SpatialIndex::ra_half_width(f64 dec_rad, f64 radius_rad)
{
    // At a given declination, a disc of angular radius r subtends an RA range of:
    //   delta_ra = r / cos(dec)
    // This expands near the poles (cos(dec) → 0), up to a maximum of π.
    //
    // We add a small margin (1°) to be conservative.
    const f64 cos_dec = std::cos(dec_rad);

    if (std::abs(cos_dec) < 1e-10)
    {
        // At the pole, all RA values are within range
        return astro_constants::kPi;
    }

    const f64 hw = (radius_rad + astro_constants::kDegToRad) / cos_dec;

    return std::min(hw, astro_constants::kPi);
}

} // namespace parallax::catalog