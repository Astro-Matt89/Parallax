/// @file solar_system_provider.cpp
/// @brief Implementation of SolarSystemProvider.

#include "universe/solar_system_provider.hpp"

#include "core/logger.hpp"

#include <cmath>
#include <functional>
#include <numbers>
#include <string>

namespace parallax::universe
{

namespace
{
    constexpr double kDegreesToRadians = std::numbers::pi / 180.0;

    /// @brief Compute angular separation (radians) between two equatorial sky positions.
    ///
    /// Uses the Vincenty great-circle formula for numerical stability at all distances.
    /// All inputs are in radians.
    [[nodiscard]] double angular_separation(double ra1, double dec1,
                                            double ra2, double dec2) noexcept
    {
        const double dra  = ra2 - ra1;
        const double cd1  = std::cos(dec1);
        const double sd1  = std::sin(dec1);
        const double cd2  = std::cos(dec2);
        const double sd2  = std::sin(dec2);
        const double cdra = std::cos(dra);
        const double sdra = std::sin(dra);

        const double a = cd2 * sdra;
        const double b = cd1 * sd2 - sd1 * cd2 * cdra;
        const double y = std::sqrt(a * a + b * b);
        const double x = sd1 * sd2 + cd1 * cd2 * cdra;
        return std::atan2(y, x);
    }
} // namespace

// =============================================================================
// update
// =============================================================================

void SolarSystemProvider::update(double jd)
{
    m_bodies     = astro::SolarSystem::compute_all(jd);
    m_moon_state = astro::SolarSystem::compute_moon_full(jd);
    m_last_jd    = jd;
    m_has_data   = true;

    PLX_CORE_TRACE("SolarSystemProvider: updated ephemeris for JD {:.4f}", jd);
}

// =============================================================================
// query_fov
// =============================================================================

void SolarSystemProvider::query_fov(double     ra,
                                    double     dec,
                                    double     radius_deg,
                                    float      /*mag_limit*/,
                                    QueryFlags flags,
                                    std::vector<CelestialObject>& results) const
{
    if (!has_flag(flags, QueryFlags::SolarSystem))
    {
        return;
    }

    if (!m_has_data)
    {
        return;
    }

    const double radius_rad = radius_deg * kDegreesToRadians;

    for (std::size_t i = 0; i < kBodyCount; ++i)
    {
        const astro::CelestialBodyState& state = body_at(i);
        const double sep = angular_separation(ra, dec,
                                              state.equatorial.ra,
                                              state.equatorial.dec);
        if (sep <= radius_rad)
        {
            results.push_back(make_object(state, i, m_moon_state.elongation_deg));
        }
    }
}

// =============================================================================
// query_object
// =============================================================================

std::optional<CelestialObject> SolarSystemProvider::query_object(u64 id) const
{
    if (decode_type(id) != ObjectType::SolarSystemBody)
    {
        return std::nullopt;
    }

    if (!m_has_data)
    {
        return std::nullopt;
    }

    const u64 source_id = decode_source_id(id);
    if (source_id >= static_cast<u64>(kBodyCount))
    {
        return std::nullopt;
    }

    const std::size_t index = static_cast<std::size_t>(source_id);
    return make_object(body_at(index), index, m_moon_state.elongation_deg);
}

// =============================================================================
// get_count
// =============================================================================

std::size_t SolarSystemProvider::get_count() const
{
    // Return kBodyCount unconditionally — the provider always manages exactly 9
    // bodies (Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune)
    // regardless of whether update() has been called yet.
    return kBodyCount;
}

// =============================================================================
// body_at (private)
// =============================================================================

const astro::CelestialBodyState& SolarSystemProvider::body_at(std::size_t index) const noexcept
{
    // index 0 → Sun, index 1 → Moon, indices 2..8 → planets[0..6]
    if (index == 0)
    {
        return m_bodies.sun;
    }
    if (index == 1)
    {
        return m_bodies.moon;
    }
    return m_bodies.planets[index - 2];
}

// =============================================================================
// make_object (private)
// =============================================================================

CelestialObject SolarSystemProvider::make_object(
    const astro::CelestialBodyState& state,
    std::size_t body_index,
    double moon_elongation_deg) noexcept
{
    CelestialObject obj;

    obj.type     = ObjectType::SolarSystemBody;
    obj.id       = encode_id(ObjectType::SolarSystemBody, static_cast<u64>(body_index));
    obj.ra       = state.equatorial.ra;   // radians, J2000 geocentric
    obj.dec      = state.equatorial.dec;  // radians, J2000 geocentric
    obj.mag_v    = state.magnitude;
    obj.color_bv = 0.0f; // B-V not provided by the ephemeris; future work could add
                         // catalogue values (e.g. Sun ≈ 0.65, Moon ≈ 0.85).

    SolarSystemData sd{};
    sd.distance_au              = static_cast<float>(state.distance_au);
    sd.phase_angle              = state.phase_angle_deg;
    sd.apparent_diameter_arcsec = state.angular_diameter_arcsec;
    sd.illumination             = state.illumination;
    // Waxing: meaningful only for the Moon (body_index == 1).
    // Waxing when elongation < 180° (Moon moving toward opposition).
    sd.waxing = (body_index == 1) ? (moon_elongation_deg < 180.0) : true;
    obj.data = sd;

    if (body_index == 0)
    {
        obj.is_container = true;
        obj.sub_universe_seed = static_cast<u64>(std::hash<std::string>{}("sun"));
        // Sun is an abstract container for its planets — planets already exist as real objects; this region
        // encloses them.
        obj.containment_angular_radius_arcsec = static_cast<float>(state.angular_diameter_arcsec / 2.0 + 100.0);
    }

    obj.parent_container_id = 0;

    return obj;
}

} // namespace parallax::universe
