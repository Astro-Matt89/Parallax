#pragma once

/// @file universe.hpp
/// @brief Universe — the public facade that ties together all four data providers.
///
/// Universe owns StarCatalogProvider, DsoCatalogProvider, SolarSystemProvider,
/// and ProceduralProvider and exposes a unified query surface to the rest of the
/// engine.  Application will talk to Universe exclusively starting from Task 7.7.

#include "universe/celestial_object.hpp"
#include "universe/data_provider.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace parallax::universe
{

// Forward-declare all four concrete providers so this header doesn't pull in
// their transitive dependencies.
class StarCatalogProvider;
class DsoCatalogProvider;
class SolarSystemProvider;
class ProceduralProvider;

// ---------------------------------------------------------------------------
// Universe
// ---------------------------------------------------------------------------

/// @brief Facade that owns and coordinates all four astronomy data providers.
///
/// Provides a single, unified query API over real catalogs (stars, DSOs, solar
/// system bodies) and the procedural star generator.  The owning Application
/// should call load_catalogs() once at startup, update() every frame, and then
/// call query_fov() to retrieve the visible objects.
class Universe
{
public:
    /// @brief Constructs the Universe and eagerly creates all four providers.
    Universe();

    /// @brief Destructor — defined in .cpp so unique_ptr sees the full provider types.
    ~Universe();

    Universe(const Universe&)            = delete;
    Universe& operator=(const Universe&) = delete;

    Universe(Universe&&) noexcept;
    Universe& operator=(Universe&&) noexcept;

    // -------------------------------------------------------------------------
    // Initialisation
    // -------------------------------------------------------------------------

    /// @brief Load all real-sky catalogs from @p data_dir.
    ///
    /// Loads Tycho-2, Hipparcos (if present), Messier DSOs, and the star name
    /// database.  Also initialises the hardcoded constellation full-name table.
    ///
    /// @param data_dir Path to the directory containing the catalog CSV files.
    /// @return true if the primary star catalog and DSO catalog loaded successfully.
    [[nodiscard]] bool load_catalogs(const std::filesystem::path& data_dir);

    /// @brief Set the master seed that drives all procedural generation.
    ///
    /// Safe to call before or after load_catalogs().  Clears any cached
    /// procedural cells so regeneration uses the new seed.
    ///
    /// @param master_seed 64-bit seed value.
    void init_procedural(std::uint64_t master_seed);

    // -------------------------------------------------------------------------
    // Per-frame update
    // -------------------------------------------------------------------------

    /// @brief Advance the solar system ephemeris to Julian Date @p julian_date.
    ///
    /// Must be called once per frame before any query_fov() call that includes
    /// QueryFlags::SolarSystem.
    ///
    /// @param julian_date Current simulation time as a Julian Date (TDT).
    void update(double julian_date);

    // -------------------------------------------------------------------------
    // Sky queries
    // -------------------------------------------------------------------------

    /// @brief Fill @p results with all objects inside the given sky cone.
    ///
    /// Clears @p results at entry so callers can safely pass a reusable buffer.
    /// Results are sorted ascending by @c mag_v (brightest first).
    ///
    /// @param ra          Cone centre right ascension (radians, J2000).
    /// @param dec         Cone centre declination (radians, J2000).
    /// @param radius_deg  Half-angle of the query cone (degrees).
    /// @param mag_limit   Faintest magnitude to include (inclusive).
    /// @param flags       Bitmask of object categories to include.
    /// @param results     Output buffer — cleared then filled.
    void query_fov(double ra,
                   double dec,
                   double radius_deg,
                   float  mag_limit,
                   QueryFlags flags,
                   std::vector<CelestialObject>& results) const;

    /// @brief Look up a single object by its packed u64 id.
    ///
    /// Decodes the type prefix and dispatches to the appropriate provider.
    /// Returns std::nullopt for ObjectType::Unknown or unrecognised prefixes.
    [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const;

    /// @brief Check whether an object's sub-universe can be resolved by the instrument.
    ///
    /// Returns false when the object does not exist, is not a container, or has no
    /// positive containment radius.
    [[nodiscard]] bool can_resolve_sub_universe(
        u64 object_id,
        double instrument_resolution_arcsec) const;

    /// @brief Return the parent container id for an object.
    ///
    /// Returns std::nullopt only when the object id cannot be resolved.
    /// A returned value of 0 means top-level (Milky Way).
    [[nodiscard]] std::optional<u64> get_parent(u64 object_id) const;

    // -------------------------------------------------------------------------
    // Name / annotation lookups
    // -------------------------------------------------------------------------

    /// @brief Return a human-readable name for the given object id.
    ///
    /// - Star:             common name from IAU list, then Bayer designation, then empty.
    /// - DeepSkyObject:    popular name (e.g. "Andromeda Galaxy") if known, else empty
    ///                     (callers may format "M<num>" themselves).
    /// - SolarSystemBody:  body name (e.g. "Sun", "Mars").
    /// - ProceduralStar:   always empty — procedural stars have no names.
    ///
    /// @return A view into stable internal storage (never dangling for the lifetime of Universe).
    [[nodiscard]] std::string_view get_name(u64 id) const;

    /// @brief Return the 3-letter IAU constellation abbreviation for sky position (ra, dec).
    ///
    /// Uses a rectangular-region table covering ~20 recognisable constellations.
    /// Returns "???" when the position does not match any entry.
    ///
    /// TODO: replace with a precise Delporte boundary implementation once boundary
    ///       data is available.
    ///
    /// @param ra  Right ascension in radians (J2000).
    /// @param dec Declination in radians (J2000).
    [[nodiscard]] std::string get_constellation(double ra, double dec) const;

    /// @brief Return the full Latin name for a 3-letter constellation abbreviation.
    ///
    /// @param abbrev  3-letter IAU abbreviation, e.g. "Ori".
    /// @return Full name (e.g. "Orion"), or empty string_view if unknown.
    [[nodiscard]] std::string_view get_constellation_full_name(std::string_view abbrev) const;

    /// @brief Resolve a HIP number directly to a CelestialObject.
    ///
    /// Forwards to the internal StarCatalogProvider for constellation-line rendering.
    /// Returns std::nullopt if Hipparcos was not loaded or @p hip is not found.
    [[nodiscard]] std::optional<CelestialObject> resolve_hip(std::uint32_t hip) const;

    // -------------------------------------------------------------------------
    // Object counts
    // -------------------------------------------------------------------------

    /// @brief Total number of real-sky objects across all catalogs.
    ///
    /// Returns stars + DSOs + solar system bodies.
    [[nodiscard]] std::size_t get_real_object_count() const;

    /// @brief Estimated number of procedural stars in the currently warm cache.
    ///
    /// This is the sum across cached HEALPix cells — not a sky-wide estimate.
    [[nodiscard]] std::size_t get_procedural_estimate() const;

    // -------------------------------------------------------------------------
    // Provider accessors (for wiring up UI / debug tools)
    // -------------------------------------------------------------------------

    /// @brief Const access to the star catalog provider.
    [[nodiscard]] const StarCatalogProvider& stars() const;

    /// @brief Const access to the DSO catalog provider.
    [[nodiscard]] const DsoCatalogProvider& dsos() const;

    /// @brief Const access to the solar system provider.
    [[nodiscard]] const SolarSystemProvider& solar_system() const;

    /// @brief Const access to the procedural provider.
    [[nodiscard]] const ProceduralProvider& procedural() const;

private:
    // -------------------------------------------------------------------------
    // Providers
    // -------------------------------------------------------------------------

    std::unique_ptr<StarCatalogProvider> stars_;
    std::unique_ptr<DsoCatalogProvider>  dsos_;
    std::unique_ptr<SolarSystemProvider> solar_;
    std::unique_ptr<ProceduralProvider>  procedural_;

    // -------------------------------------------------------------------------
    // Name databases
    // -------------------------------------------------------------------------

    /// HIP number → common name (e.g. 32349 → "Sirius").
    std::unordered_map<std::uint32_t, std::string> hip_names_;

    /// HIP number → Bayer designation (e.g. 32349 → "α CMa").
    std::unordered_map<std::uint32_t, std::string> hip_bayer_;

    /// Messier number → popular name (e.g. 31 → "Andromeda Galaxy").
    std::unordered_map<std::uint32_t, std::string> messier_names_;

    /// Solar system body names indexed by body_id (0=Sun, 1=Moon, 2=Mercury, …, 8=Neptune).
    std::array<std::string, 9> body_names_;

    /// 3-letter IAU abbreviation → full Latin name (all 88 constellations).
    std::unordered_map<std::string, std::string> constellation_full_names_;

    // -------------------------------------------------------------------------
    // Procedural state
    // -------------------------------------------------------------------------

    bool procedural_initialized_{false};

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /// @brief Load the star name / Bayer designation database from a CSV file.
    ///
    /// Tolerates UTF-8 BOM, empty fields, comment lines starting with '#',
    /// and trailing whitespace.  Populates hip_names_ and hip_bayer_.
    void load_name_database(const std::filesystem::path& csv_path);

    /// @brief Populate messier_names_ with well-known popular names.
    void init_messier_names();

    /// @brief Populate constellation_full_names_ with all 88 IAU entries.
    void init_constellation_full_names();
};

} // namespace parallax::universe
