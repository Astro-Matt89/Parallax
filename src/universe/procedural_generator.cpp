// universe/procedural_generator.cpp
#include "procedural_generator.hpp"
#include "core/math/coordinates.hpp"
#include <cmath>

namespace parallax {

// -----------------------------------------------------------------------
// IMF helpers
// -----------------------------------------------------------------------
double sampleKroupaMass(PcgRng& rng) {
    // Kroupa (2001) broken power-law IMF: two-segment inverse CDF
    // α1 = 1.3 for m < 0.5 M☉, α2 = 2.3 for m >= 0.5 M☉
    // Approximate via rejection-free piece-wise inverse CDF.
    constexpr double m_break = 0.5;
    constexpr double alpha1  = 1.3;
    constexpr double alpha2  = 2.3;

    // Normalisation weights for each segment (0.1..0.5 and 0.5..150)
    const double w1 = (std::pow(m_break, 1.0-alpha1) - std::pow(0.1, 1.0-alpha1))
                     / (1.0 - alpha1);
    const double w2 = (std::pow(150.0, 1.0-alpha2) - std::pow(m_break, 1.0-alpha2))
                     / (1.0 - alpha2);
    const double fracLow = w1 / (w1 + w2);

    double u = rng.nextDouble();
    if (u < fracLow) {
        // Low-mass segment
        double v = rng.nextDouble();
        double base = std::pow(0.1, 1.0-alpha1);
        double top  = std::pow(m_break, 1.0-alpha1);
        return std::pow(base + v*(top - base), 1.0/(1.0 - alpha1));
    } else {
        // High-mass segment
        double v = rng.nextDouble();
        double base = std::pow(m_break, 1.0-alpha2);
        double top  = std::pow(150.0, 1.0-alpha2);
        return std::pow(base + v*(top - base), 1.0/(1.0 - alpha2));
    }
}

SpectralClass spectralClassFromMass(double m) {
    if (m >= 16.0) return SpectralClass::O;
    if (m >=  2.1) return SpectralClass::B;
    if (m >=  1.4) return SpectralClass::A;
    if (m >=  1.04) return SpectralClass::F;
    if (m >=  0.8) return SpectralClass::G;
    if (m >=  0.45) return SpectralClass::K;
    if (m >=  0.08) return SpectralClass::M;
    return SpectralClass::L; // brown dwarf
}

double absMagnitudeFromSpectralClass(SpectralClass sc) {
    switch (sc) {
        case SpectralClass::O:  return -5.0;
        case SpectralClass::B:  return -1.5;
        case SpectralClass::A:  return  2.0;
        case SpectralClass::F:  return  3.5;
        case SpectralClass::G:  return  5.0;
        case SpectralClass::K:  return  6.5;
        case SpectralClass::M:  return  9.0;
        case SpectralClass::L:  return 14.0;
        default:                return  5.0;
    }
}

// -----------------------------------------------------------------------
// ProceduralGenerator
// -----------------------------------------------------------------------
ProceduralGenerator::ProceduralGenerator(uint64_t master_seed, double mag_limit)
    : m_master_seed(master_seed), m_mag_limit(mag_limit) {}

uint64_t ProceduralGenerator::tileSeed(int tile_ra, int tile_dec) const {
    // Mix tile coordinates with master seed
    uint64_t h = m_master_seed;
    h ^= static_cast<uint64_t>(tile_ra  + 0xFFFF) * 6364136223846793005ULL;
    h ^= static_cast<uint64_t>(tile_dec + 0xFFFF) * 1442695040888963407ULL;
    return h;
}

double ProceduralGenerator::stellarDensity(double /*ra_deg*/, double dec_deg,
                                            double /*observer_dist_pc*/) {
    // Simplified: more stars near galactic plane.
    // Galactic latitude approximation from declination (rough for demo).
    // A proper implementation would use galactic coordinates.
    // Use dec as proxy (densest near equator for Milky Way mid-latitudes).
    static constexpr double kGalacticEquatorOffsetDeg = 28.0; // rough galactic plane dec offset
    double b_approx = std::abs(dec_deg - (-kGalacticEquatorOffsetDeg));
    double density = 500.0 * std::exp(-b_approx / 25.0) + 30.0;
    return density; // stars per sq degree down to V~12
}

Star ProceduralGenerator::generateStar(double ra_deg, double dec_deg,
                                        double dist_pc, uint64_t id) const {
    PcgRng rng(m_master_seed ^ id);

    double mass = sampleKroupaMass(rng);
    SpectralClass sc = spectralClassFromMass(mass);
    double abs_mag = absMagnitudeFromSpectralClass(sc);

    // Add scatter in absolute magnitude (±0.5 mag)
    abs_mag += rng.nextInRange(-0.5, 0.5);

    // Distance modulus
    double v_mag = abs_mag + 5.0 * std::log10(dist_pc / 10.0);

    Star s;
    s.id             = id;
    s.position       = {ra_deg, dec_deg};
    s.distance_pc    = dist_pc;
    s.v_magnitude    = v_mag;
    s.abs_magnitude  = abs_mag;
    s.spectral_class = sc;
    s.is_procedural  = true;
    if (dist_pc > 0.0) {
        s.parallax_mas = 1000.0 / dist_pc;
    }
    // Occasional variable star (~5%)
    s.is_variable = (rng.nextUInt(20) == 0);
    return s;
}

std::vector<Star> ProceduralGenerator::generateTile(double tile_ra_deg,
                                                      double tile_dec_deg,
                                                      double tile_size_deg,
                                                      double observer_dist_pc) const {
    int tile_ra  = static_cast<int>(std::floor(tile_ra_deg  / tile_size_deg));
    int tile_dec = static_cast<int>(std::floor(tile_dec_deg / tile_size_deg));

    PcgRng rng(tileSeed(tile_ra, tile_dec));

    double density = stellarDensity(tile_ra_deg, tile_dec_deg, observer_dist_pc);
    double area    = tile_size_deg * tile_size_deg;
    int    n_stars = static_cast<int>(density * area);

    // Poisson-like jitter: ±20%
    n_stars = static_cast<int>(n_stars * (0.8 + rng.nextDouble() * 0.4));

    std::vector<Star> result;
    result.reserve(static_cast<std::size_t>(n_stars));

    // Base ID for this tile
    uint64_t base_id = (static_cast<uint64_t>(tile_ra + 0x8000) << 20) |
                       (static_cast<uint64_t>(tile_dec + 0x8000));
    base_id = base_id * 1000000ULL + m_master_seed;

    for (int i = 0; i < n_stars; ++i) {
        double ra  = tile_ra_deg  + rng.nextDouble() * tile_size_deg;
        double dec = tile_dec_deg + rng.nextDouble() * tile_size_deg;
        dec = std::clamp(dec, -90.0, 90.0);
        ra  = std::fmod(ra + 360.0, 360.0);

        // Distance: draw from a log-uniform distribution (10 to 5000 pc)
        double log_dist = rng.nextInRange(std::log10(10.0), std::log10(5000.0));
        double dist_pc  = std::pow(10.0, log_dist);

        Star s = generateStar(ra, dec, dist_pc, base_id + static_cast<uint64_t>(i));
        if (s.v_magnitude <= m_mag_limit) {
            result.push_back(std::move(s));
        }
    }

    return result;
}

} // namespace parallax
