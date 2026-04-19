#pragma once

/// @file solar_system_provider.hpp
/// @brief SolarSystemProvider — wraps the Sprint 06 SolarSystem ephemeris behind DataProvider.
///
/// Caches the last computed AllBodies snapshot and exposes the 9 bodies (Sun, Moon,
/// Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune) through the DataProvider
/// interface.  The SolarSystem class itself is untouched — this is a pure wrapper.

#include "universe/data_provider.hpp"
#include "astro/solar_system.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace parallax::universe
{

/// @brief DataProvider implementation wrapping the Sprint 06 SolarSystem ephemeris.
///
/// Ownership model:
///   - @c m_bodies holds the last @c AllBodies snapshot from @c SolarSystem::compute_all().
///   - @c m_has_data is @c false until the first call to @c update().
///
/// Body index encoding (source_id field of the packed u64 id):
///   0 → Sun
///   1 → Moon
///   2 → Mercury (planets[0])
///   3 → Venus   (planets[1])
///   4 → Mars    (planets[2])
///   5 → Jupiter (planets[3])
///   6 → Saturn  (planets[4])
///   7 → Uranus  (planets[5])
///   8 → Neptune (planets[6])
///
/// Solar system bodies are ALWAYS returned by query_fov() regardless of mag_limit when
/// they fall within the query radius (they have privileged visibility per spec).
class SolarSystemProvider final : public DataProvider
{
public:
    SolarSystemProvider()                                              = default;
    ~SolarSystemProvider() override                                    = default;

    SolarSystemProvider(const SolarSystemProvider&)                   = delete;
    SolarSystemProvider& operator=(const SolarSystemProvider&)        = delete;

    SolarSystemProvider(SolarSystemProvider&&)                        = default;
    SolarSystemProvider& operator=(SolarSystemProvider&&)             = default;

    // -------------------------------------------------------------------------
    // Ephemeris update
    // -------------------------------------------------------------------------

    /// @brief Compute and cache all Solar System body positions for the given JD.
    ///
    /// Must be called once per frame (or at any time the simulation clock advances)
    /// before query_fov() or query_object() are meaningful.  Repeated calls with the
    /// same JD are safe — there is no skip-on-match optimisation; recompute is cheap.
    ///
    /// @param jd Julian Date (TDT).
    void update(double jd);

    // -------------------------------------------------------------------------
    // DataProvider overrides
    // -------------------------------------------------------------------------

    /// @brief Append all Solar System bodies inside the query cone to @p results.
    ///
    /// Returns immediately if @p flags does not include QueryFlags::SolarSystem, or if
    /// update() has not yet been called.
    ///
    /// @note Solar system bodies are always included when within the query radius —
    ///       @p mag_limit is intentionally ignored for this provider.
    ///
    /// @param ra         Cone centre RA (radians, J2000).
    /// @param dec        Cone centre Dec (radians, J2000).
    /// @param radius_deg Half-angle of the query cone (degrees).
    /// @param mag_limit  Ignored — solar system bodies bypass magnitude filtering.
    /// @param flags      Bitmask of object categories.
    /// @param results    Output vector — objects are appended, never cleared.
    void query_fov(double     ra,
                   double     dec,
                   double     radius_deg,
                   float      mag_limit,
                   QueryFlags flags,
                   std::vector<CelestialObject>& results) const override;

    /// @brief Look up a single Solar System body by its packed u64 id.
    ///
    /// Returns @c std::nullopt if the type prefix decoded from @p id is not
    /// ObjectType::SolarSystemBody, if update() has not been called, or if the
    /// decoded body index is out of range [0, kBodyCount).
    [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const override;

    /// @brief Total number of Solar System bodies this provider manages.
    ///
    /// Returns kBodyCount (9) unconditionally — the provider knows about all 9 bodies
    /// at construction time, even before the first update() call.
    [[nodiscard]] std::size_t get_count() const override;

private:
    /// Total number of bodies: Sun + Moon + 7 planets (Mercury..Neptune).
    static constexpr std::size_t kBodyCount = 9;

    /// @brief Convert a cached body state and its index to a CelestialObject.
    [[nodiscard]] static CelestialObject make_object(
        const astro::CelestialBodyState& state,
        std::size_t body_index) noexcept;

    /// @brief Retrieve a cached body state by index (0..kBodyCount-1).
    ///
    /// Caller is responsible for bounds-checking before calling.
    [[nodiscard]] const astro::CelestialBodyState& body_at(std::size_t index) const noexcept;

    astro::SolarSystem::AllBodies m_bodies{};       ///< Last computed ephemeris snapshot
    double                        m_last_jd{0.0};   ///< JD of the last update() call
    bool                          m_has_data{false}; ///< True once update() has been called
};

} // namespace parallax::universe
