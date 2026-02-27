// universe/star_catalog.cpp
#include "star_catalog.hpp"
#include "core/math/coordinates.hpp"
#include <cctype>

namespace parallax {

// -----------------------------------------------------------------------
// addStar
// -----------------------------------------------------------------------
void StarCatalog::addStar(Star s) {
    std::size_t idx = m_stars.size();
    m_stars.push_back(std::move(s));

    auto [ra_c, dec_c] = cellForPosition(m_stars.back().position);
    m_grid[cellKey(ra_c, dec_c)].push_back(idx);
}

// -----------------------------------------------------------------------
// query
// -----------------------------------------------------------------------
std::vector<const Star*> StarCatalog::query(const Equatorial& centre,
                                             double radius_deg,
                                             double mag_limit) const {
    std::vector<const Star*> result;

    // Number of grid cells to check in each axis
    int cells = static_cast<int>(std::ceil(radius_deg / kGridResolutionDeg)) + 1;

    auto [cra, cdec] = cellForPosition(centre);

    for (int dra = -cells; dra <= cells; ++dra) {
        for (int ddec = -cells; ddec <= cells; ++ddec) {
            int ra_cell  = cra  + dra;
            int dec_cell = cdec + ddec;
            // Wrap RA around 360 degrees
            int max_ra_cells = static_cast<int>(360.0 / kGridResolutionDeg);
            ra_cell = ((ra_cell % max_ra_cells) + max_ra_cells) % max_ra_cells;
            // Clamp Dec
            int max_dec_cells = static_cast<int>(180.0 / kGridResolutionDeg);
            dec_cell = std::clamp(dec_cell, 0, max_dec_cells - 1);

            auto it = m_grid.find(cellKey(ra_cell, dec_cell));
            if (it == m_grid.end()) continue;

            for (std::size_t idx : it->second) {
                const Star& star = m_stars[idx];
                if (star.v_magnitude > mag_limit) continue;
                if (angularSeparation(centre, star.position) <= radius_deg) {
                    result.push_back(&star);
                }
            }
        }
    }

    // Sort brightest first
    std::sort(result.begin(), result.end(),
              [](const Star* a, const Star* b) {
                  return a->v_magnitude < b->v_magnitude;
              });
    return result;
}

// -----------------------------------------------------------------------
// findById
// -----------------------------------------------------------------------
const Star* StarCatalog::findById(uint64_t id) const {
    for (const auto& s : m_stars) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// findByName
// -----------------------------------------------------------------------
const Star* StarCatalog::findByName(std::string_view name) const {
    for (const auto& s : m_stars) {
        if (s.name.size() != name.size()) continue;
        bool match = true;
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (std::tolower((unsigned char)s.name[i]) !=
                std::tolower((unsigned char)name[i])) {
                match = false; break;
            }
        }
        if (match) return &s;
    }
    return nullptr;
}

// -----------------------------------------------------------------------
// loadBuiltin - a representative sample of bright Hipparcos stars
// -----------------------------------------------------------------------
StarCatalog StarCatalog::loadBuiltin() {
    StarCatalog cat;

    // Format: {id, name, ra_deg, dec_deg, dist_pc, v_mag, spectral_class}
    struct Entry {
        uint64_t    id;
        const char* name;
        double      ra_deg, dec_deg;
        double      dist_pc, v_mag;
        SpectralClass sc;
    };

    static const Entry entries[] = {
        {  87937, "Barnard's Star",   269.452,   4.693,   1.83,  9.54, SpectralClass::M },
        {  32349, "Sirius",           101.287, -16.716,   2.64, -1.46, SpectralClass::A },
        {  70890, "Proxima Centauri", 217.429, -62.679,   1.30, 11.13, SpectralClass::M },
        {  71683, "Alpha Centauri A", 219.902, -60.834,   1.34, -0.01, SpectralClass::G },
        {  71681, "Alpha Centauri B", 219.902, -60.834,   1.34,  1.33, SpectralClass::K },
        {  24436, "Rigel",             78.634,  -8.202, 264.0,   0.18, SpectralClass::B },
        {  27989, "Betelgeuse",        88.793,   7.407, 197.0,   0.42, SpectralClass::M },
        {  49669, "Regulus",          152.093,  11.967,  77.5,   1.35, SpectralClass::B },
        {  65474, "Spica",            201.298, -11.161, 250.0,   0.97, SpectralClass::B },
        {  69673, "Arcturus",         213.915,  19.182,  11.3,  -0.05, SpectralClass::K },
        {  91262, "Vega",             279.235,  38.784,   7.68,  0.03, SpectralClass::A },
        {  97649, "Altair",           297.696,   8.868,   5.13,  0.76, SpectralClass::A },
        { 113368, "Fomalhaut",        344.413, -29.622,   7.69,  1.16, SpectralClass::A },
        {  11767, "Polaris",           37.954,  89.264, 133.0,   1.97, SpectralClass::F },
        {  80763, "Antares",          247.352, -26.432, 170.0,   1.06, SpectralClass::M },
        {  37279, "Procyon",          114.827,   5.225,   3.51,  0.34, SpectralClass::F },
        {  30438, "Canopus",           95.988, -52.696, 310.0,  -0.72, SpectralClass::A },
        {   9884, "Achernar",          24.429, -57.237,  44.0,   0.46, SpectralClass::B },
        {  68702, "Hadar",            210.956, -60.373, 161.0,   0.61, SpectralClass::B },
        {  60718, "Acrux",            186.650, -63.099, 321.0,   0.76, SpectralClass::B },
        {  25336, "Aldebaran",         68.980,  16.509,  20.0,   0.87, SpectralClass::K },
        {  36850, "Castor",           113.650,  31.889,  15.6,   1.58, SpectralClass::A },
        {  37826, "Pollux",           116.329,  28.026,  10.3,   1.14, SpectralClass::K },
        { 102098, "Deneb",            310.358,  45.280, 802.0,   1.25, SpectralClass::A },
    };

    for (const auto& e : entries) {
        Star s;
        s.id             = e.id;
        s.name           = e.name;
        s.position       = {e.ra_deg, e.dec_deg};
        s.distance_pc    = e.dist_pc;
        s.v_magnitude    = e.v_mag;
        s.spectral_class = e.sc;
        if (e.dist_pc > 0.0) {
            // absolute magnitude from distance modulus: m - M = 5*log10(d/10)
            s.abs_magnitude = e.v_mag - 5.0 * std::log10(e.dist_pc / 10.0);
            s.parallax_mas  = 1000.0 / e.dist_pc;
        }
        cat.addStar(std::move(s));
    }

    return cat;
}

// -----------------------------------------------------------------------
// rebuildGrid (not called externally in this impl, but available)
// -----------------------------------------------------------------------
void StarCatalog::rebuildGrid() {
    m_grid.clear();
    for (std::size_t i = 0; i < m_stars.size(); ++i) {
        auto [ra_c, dec_c] = cellForPosition(m_stars[i].position);
        m_grid[cellKey(ra_c, dec_c)].push_back(i);
    }
}

} // namespace parallax
