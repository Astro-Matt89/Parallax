#pragma once
// universe/procedural_generator.hpp - Deterministic procedural universe layer
//
// Generates stars, galaxies, and deep-sky objects for regions not covered by
// real catalogs. All generation is seeded and reproducible.

#include "star.hpp"
#include <cstdint>
#include <vector>

namespace parallax {

// -----------------------------------------------------------------------
// Fast PCG-based pseudorandom number generator (PCG-XSH-RR 32/64)
// -----------------------------------------------------------------------
class PcgRng {
public:
    explicit PcgRng(uint64_t seed, uint64_t stream = 1)
        : m_state(seed + (stream | 1)), m_inc((stream << 1) | 1) {
        advance();
    }

    uint32_t next() {
        uint64_t old = m_state;
        m_state = old * 6364136223846793005ULL + m_inc;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    /// Uniform float in [0,1)
    double nextDouble() {
        return static_cast<double>(next()) / 4294967296.0;
    }

    /// Uniform double in [lo, hi)
    double nextInRange(double lo, double hi) {
        return lo + nextDouble() * (hi - lo);
    }

    /// Integer in [0, n)
    uint32_t nextUInt(uint32_t n) {
        return next() % n;
    }

private:
    uint64_t m_state;
    uint64_t m_inc;
    void advance() { next(); }
};

// -----------------------------------------------------------------------
// Initial Mass Function helpers (Kroupa 2001)
// -----------------------------------------------------------------------

/// Sample stellar mass [solar masses] from a Kroupa IMF.
double sampleKroupaMass(PcgRng& rng);

/// Approximate spectral class from stellar mass.
SpectralClass spectralClassFromMass(double mass_solar);

/// Approximate visual absolute magnitude from spectral class (main sequence).
double absMagnitudeFromSpectralClass(SpectralClass sc);

// -----------------------------------------------------------------------
// ProceduralGenerator
// -----------------------------------------------------------------------
class ProceduralGenerator {
public:
    /// @param master_seed  Global universe seed (same seed = same universe)
    /// @param mag_limit    Faintest apparent magnitude to generate
    explicit ProceduralGenerator(uint64_t master_seed, double mag_limit = 12.0);

    /// Generate stars for a given sky tile.
    /// @param tile_ra_deg   Left edge RA of tile [degrees]
    /// @param tile_dec_deg  Bottom edge Dec of tile [degrees]
    /// @param tile_size_deg Tile side length [degrees]
    /// @param observer_dist_pc  Assumed observer distance from galactic centre [pc]
    ///                          (controls stellar density per steradian)
    std::vector<Star> generateTile(double tile_ra_deg,
                                   double tile_dec_deg,
                                   double tile_size_deg,
                                   double observer_dist_pc = 8500.0) const;

    /// Generate a single random star at (ra_deg, dec_deg) with a given
    /// distance (used internally and for testing).
    Star generateStar(double ra_deg, double dec_deg,
                      double dist_pc, uint64_t id) const;

    uint64_t masterSeed() const { return m_master_seed; }
    double   magLimit()   const { return m_mag_limit; }

private:
    uint64_t m_master_seed;
    double   m_mag_limit;

    /// Number of stars expected per square degree down to mag_limit
    /// from a simplified galactic model.
    static double stellarDensity(double ra_deg, double dec_deg,
                                  double observer_dist_pc);

    /// Seed for a specific sky tile (deterministic).
    uint64_t tileSeed(int tile_ra, int tile_dec) const;
};

} // namespace parallax
