// tests/test_procedural.cpp
#include "test_main.hpp"
#include "universe/procedural_generator.hpp"
#include <cmath>
#include <unordered_set>

using namespace parallax;

int testProcedural() {
    constexpr uint64_t kSeed = 0xDEADBEEF12345678ULL;
    ProceduralGenerator gen(kSeed, 12.0);

    // -----------------------------------------------------------------------
    // PcgRng basic tests
    // -----------------------------------------------------------------------
    PcgRng rng1(42, 1);
    PcgRng rng2(42, 1);
    // Same seed -> same sequence
    for (int i = 0; i < 100; ++i) {
        uint32_t v1 = rng1.next();
        uint32_t v2 = rng2.next();
        test::check(v1 == v2, "PCG same seed same output #" + std::to_string(i));
        if (v1 != v2) break; // don't spam failures
    }

    // Different seed -> different sequence
    PcgRng rng_ref(42, 1);
    PcgRng rng3(99, 1);
    bool any_diff = false;
    for (int i = 0; i < 10; ++i) {
        if (rng3.next() != rng_ref.next()) { any_diff = true; break; }
    }
    test::check(any_diff, "PCG different seeds produce different output");

    // nextDouble in [0,1)
    PcgRng rng4(1234, 1);
    bool all_in_range = true;
    for (int i = 0; i < 1000; ++i) {
        double v = rng4.nextDouble();
        if (v < 0.0 || v >= 1.0) { all_in_range = false; break; }
    }
    test::check(all_in_range, "nextDouble always in [0,1)");

    // -----------------------------------------------------------------------
    // IMF mass sampling
    // -----------------------------------------------------------------------
    PcgRng rng5(777, 1);
    bool all_positive = true;
    bool has_low_mass = false, has_high_mass = false;
    for (int i = 0; i < 10000; ++i) {
        double m = sampleKroupaMass(rng5);
        if (m <= 0.0) { all_positive = false; break; }
        if (m < 0.5) has_low_mass = true;
        if (m > 2.0) has_high_mass = true;
    }
    test::check(all_positive, "IMF mass always positive");
    test::check(has_low_mass, "IMF produces low-mass stars (<0.5 Msun)");
    test::check(has_high_mass, "IMF produces high-mass stars (>2 Msun)");

    // -----------------------------------------------------------------------
    // Determinism: same tile, same seed -> identical stars
    // -----------------------------------------------------------------------
    auto tile1 = gen.generateTile(80.0, -10.0, 4.0);
    auto tile2 = gen.generateTile(80.0, -10.0, 4.0);
    test::check(tile1.size() == tile2.size(),
                "determinism: same tile size");
    bool ids_match = true;
    for (std::size_t i = 0; i < tile1.size() && i < tile2.size(); ++i) {
        if (tile1[i].id != tile2[i].id ||
            tile1[i].v_magnitude != tile2[i].v_magnitude) {
            ids_match = false; break;
        }
    }
    test::check(ids_match, "determinism: same tile, same star IDs and magnitudes");

    // -----------------------------------------------------------------------
    // Different tiles -> different IDs
    // -----------------------------------------------------------------------
    auto tile3 = gen.generateTile(0.0, 80.0, 4.0); // near north pole
    std::unordered_set<uint64_t> ids1;
    for (const auto& s : tile1) ids1.insert(s.id);
    bool any_overlap = false;
    for (const auto& s : tile3) {
        if (ids1.count(s.id)) { any_overlap = true; break; }
    }
    test::check(!any_overlap, "different tiles produce different IDs");

    // -----------------------------------------------------------------------
    // Generated stars: magnitude limit respected
    // -----------------------------------------------------------------------
    bool all_within_limit = true;
    for (const auto& s : tile1) {
        if (s.v_magnitude > gen.magLimit() + 1e-6) {
            all_within_limit = false; break;
        }
    }
    test::check(all_within_limit, "all generated stars within mag limit");

    // -----------------------------------------------------------------------
    // Generated stars: positions within tile
    // -----------------------------------------------------------------------
    bool all_in_tile = true;
    for (const auto& s : tile1) {
        double ra  = s.position.ra_deg;
        double dec = s.position.dec_deg;
        if (dec < -90.0 || dec > 90.0) { all_in_tile = false; break; }
        // RA can wrap, so just check it's normalised
        if (ra < 0.0 || ra >= 360.0) { all_in_tile = false; break; }
    }
    test::check(all_in_tile, "generated star positions are valid");

    // -----------------------------------------------------------------------
    // Variable star flag: approximately ~5%
    // -----------------------------------------------------------------------
    int n_variable = 0;
    for (const auto& s : tile1) if (s.is_variable) ++n_variable;
    double var_frac = tile1.empty() ? 0.0
                     : static_cast<double>(n_variable) / tile1.size();
    test::check(var_frac < 0.20, "variable star fraction < 20%");

    // -----------------------------------------------------------------------
    // Spectral class distribution: G stars should exist
    // -----------------------------------------------------------------------
    bool has_G = false;
    for (const auto& s : tile1) {
        if (s.spectral_class == SpectralClass::G) { has_G = true; break; }
    }
    test::check(has_G || tile1.empty(), "spectral classes include G-type stars");

    return 0;
}
