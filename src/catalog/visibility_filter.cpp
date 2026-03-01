/// @file visibility_filter.cpp
/// @brief Implementation of fast visibility prefilter for large star catalogs.

#include "catalog/visibility_filter.hpp"

#include "core/types.hpp"

#include <cmath>

namespace parallax::catalog
{

// -----------------------------------------------------------------
// Main filter entry point
// -----------------------------------------------------------------

std::vector<u32> VisibilityFilter::filter(
    std::span<const StarEntry> stars,
    const astro::ObserverLocation& observer,
    f64 lst,
    f32 mag_limit,
    PrefilterStats* stats)
{
    const f64 lat = observer.latitude_rad;
    const u32 total = static_cast<u32>(stars.size());

    // Pre-allocate for ~50% pass rate (typical for full-sky catalog)
    std::vector<u32> result;
    result.reserve(total / 2);

    u32 skipped_mag = 0;
    u32 skipped_never_rises = 0;
    u32 skipped_below_hz = 0;

    for (u32 i = 0; i < total; ++i)
    {
        const auto& star = stars[i];

        // -----------------------------------------------------------------
        // 1. Early magnitude cull (cheapest check first)
        // -----------------------------------------------------------------
        if (star.mag_v > mag_limit)
        {
            ++skipped_mag;
            continue;
        }

        // -----------------------------------------------------------------
        // 2. Never-rises check: skip stars whose max altitude is always < -1°
        //    alt_max = 90° - |lat - dec|
        //    This catches ~50% of the catalog for mid-latitude observers.
        // -----------------------------------------------------------------
        if (!can_rise(star.dec, lat))
        {
            ++skipped_never_rises;
            continue;
        }

        // -----------------------------------------------------------------
        // 3. Hour-angle check: skip stars currently below the horizon
        //    Uses the setting hour angle formula with a 15° margin.
        // -----------------------------------------------------------------
        if (!is_above_horizon(star.ra, star.dec, lat, lst))
        {
            ++skipped_below_hz;
            continue;
        }

        // Star passes prefilter — needs full transform
        result.push_back(i);
    }

    // -----------------------------------------------------------------
    // Fill stats if requested
    // -----------------------------------------------------------------
    if (stats != nullptr)
    {
        stats->total = total;
        stats->passed = static_cast<u32>(result.size());
        stats->skipped_never_rises = skipped_never_rises;
        stats->skipped_below_hz = skipped_below_hz;
        stats->skipped_mag = skipped_mag;
    }

    return result;
}

// -----------------------------------------------------------------
// can_rise: check if star ever reaches above the horizon
// -----------------------------------------------------------------

bool VisibilityFilter::can_rise(f64 dec_rad, f64 lat_rad)
{
    // Maximum altitude for a star at declination δ, observed from latitude φ:
    //   alt_max = 90° - |φ - δ|  (in degrees)
    //
    // In radians: alt_max = π/2 - |φ - δ|
    //
    // If alt_max < margin → star never rises high enough to be visible.
    // Use -1° margin to account for atmospheric refraction (~34' at horizon).

    constexpr f64 kMarginRad = -1.0 * astro_constants::kDegToRad; // -1°

    const f64 alt_max = astro_constants::kHalfPi - std::abs(lat_rad - dec_rad);
    return alt_max > kMarginRad;
}

// -----------------------------------------------------------------
// is_above_horizon: hour angle check for current visibility
// -----------------------------------------------------------------

bool VisibilityFilter::is_above_horizon(
    f64 ra_rad, f64 dec_rad, f64 lat_rad, f64 lst)
{
    // Setting hour angle formula:
    //   cos(H_set) = -tan(φ) × tan(δ)
    //
    // Special cases:
    //   |cos(H_set)| >= 1 and tan(φ)*tan(δ) < 0 → circumpolar (always above) → pass
    //   |cos(H_set)| >= 1 and tan(φ)*tan(δ) > 0 → never rises → fail
    //                                               (should already be caught by can_rise)

    const f64 cos_h_set = -std::tan(lat_rad) * std::tan(dec_rad);

    if (cos_h_set <= -1.0)
    {
        // Circumpolar: always above horizon → pass
        return true;
    }

    if (cos_h_set >= 1.0)
    {
        // Never rises (should have been caught by can_rise, but be safe)
        return false;
    }

    // H_set is the half-width of time the star is above the horizon
    const f64 h_set = std::acos(cos_h_set);

    // Add a generous margin (15° ≈ 1 hour of RA) so we don't clip
    // stars that are just rising/setting into the FOV edge
    constexpr f64 kMarginRad = 15.0 * astro_constants::kDegToRad;
    const f64 h_set_margined = h_set + kMarginRad;

    // Current hour angle of this star: H = LST - RA
    f64 hour_angle = lst - ra_rad;

    // Normalize to [-π, +π]
    while (hour_angle > astro_constants::kPi)
    {
        hour_angle -= astro_constants::kTwoPi;
    }
    while (hour_angle < -astro_constants::kPi)
    {
        hour_angle += astro_constants::kTwoPi;
    }

    // Star is above horizon if |H| < H_set (with margin)
    return std::abs(hour_angle) < h_set_margined;
}

} // namespace parallax::catalog