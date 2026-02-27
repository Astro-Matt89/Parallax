#pragma once
// universe/star_catalog.hpp - In-memory stellar catalog with spatial indexing
//
// The catalog holds both real (catalog) stars and procedurally generated ones.
// Spatial queries are accelerated by a grid-based cell index over (RA, Dec).

#include "star.hpp"
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>
#include <algorithm>
#include <string_view>
#include <optional>

namespace parallax {

// -----------------------------------------------------------------------
// StarCatalog
// -----------------------------------------------------------------------
class StarCatalog {
public:
    // Resolution of the spatial grid (degrees per cell)
    static constexpr double kGridResolutionDeg = 1.0;

    /// Add a star to the catalog.
    void addStar(Star s);

    /// Retrieve all stars within a circular field of view.
    /// @param centre   Field centre (equatorial)
    /// @param radius_deg  Search radius [degrees]
    /// @param mag_limit   Faintest magnitude to return
    std::vector<const Star*> query(const Equatorial& centre,
                                   double radius_deg,
                                   double mag_limit = 99.0) const;

    /// Retrieve a star by its catalog ID (returns nullptr if not found).
    const Star* findById(uint64_t id) const;

    /// Retrieve a star by name (case-insensitive, first match).
    const Star* findByName(std::string_view name) const;

    /// Total number of stars in the catalog.
    std::size_t size() const { return m_stars.size(); }

    /// Access all stars (read-only).
    const std::vector<Star>& stars() const { return m_stars; }

    /// Load a minimal built-in bright-star table (Hipparcos bright stars, V<6).
    static StarCatalog loadBuiltin();

private:
    std::vector<Star> m_stars;

    // Spatial grid: key = (ra_cell, dec_cell) packed into 32 bits
    using CellKey = uint32_t;
    std::unordered_map<CellKey, std::vector<std::size_t>> m_grid;

    static CellKey cellKey(int ra_cell, int dec_cell) {
        return static_cast<uint32_t>((ra_cell & 0xFFFF) << 16) |
               static_cast<uint32_t>(dec_cell & 0xFFFF);
    }

    static std::pair<int,int> cellForPosition(const Equatorial& eq) {
        int ra  = static_cast<int>(std::floor(eq.ra_deg  / kGridResolutionDeg));
        int dec = static_cast<int>(std::floor((eq.dec_deg + 90.0) / kGridResolutionDeg));
        return {ra, dec};
    }

    void rebuildGrid();
};

} // namespace parallax
