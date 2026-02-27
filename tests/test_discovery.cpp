// tests/test_discovery.cpp
#include "test_main.hpp"
#include "discovery/discovery_engine.hpp"
#include "universe/star_catalog.hpp"
#include <cmath>

using namespace parallax;

int testDiscovery() {
    // -----------------------------------------------------------------------
    // Transit depth formula
    // -----------------------------------------------------------------------
    // Earth around Sun: r_planet=1 Re, r_star=1 Rs -> depth ≈ (0.00916)^2
    double depth_earth = DiscoveryEngine::transitDepth(1.0, 1.0);
    test::checkNear(depth_earth, 0.00916 * 0.00916, 1e-6,
                    "Earth transit depth");

    // Jupiter: r~11 Re, depth ~ (11*0.00916)^2 ≈ 0.0101^2 ≈ 0.01009
    double depth_jupiter = DiscoveryEngine::transitDepth(11.0, 1.0);
    test::checkNear(depth_jupiter, 0.01009, 0.001, "Jupiter transit depth");

    // Larger planet -> larger depth
    test::check(depth_jupiter > depth_earth, "larger planet deeper transit");

    // -----------------------------------------------------------------------
    // Minimum detectable planet radius
    // -----------------------------------------------------------------------
    // At SNR=100, min radius is much smaller than at SNR=10
    double r100 = DiscoveryEngine::minimumDetectablePlanetRadius(100.0, 1.0);
    double r10  = DiscoveryEngine::minimumDetectablePlanetRadius(10.0, 1.0);
    test::check(r100 < r10, "higher SNR -> smaller detectable planet");
    // For M-dwarf (0.2 Rs), smaller minimum radius
    double r_mdwarf = DiscoveryEngine::minimumDetectablePlanetRadius(100.0, 0.2);
    test::check(r_mdwarf < r100, "M-dwarf host -> smaller detectable planet");

    // -----------------------------------------------------------------------
    // Parallax detection limit
    // -----------------------------------------------------------------------
    Telescope scope1m = make1mReflector();
    double limit6 = DiscoveryEngine::parallaxDetectionLimit_mas(scope1m, 6);
    double limit12= DiscoveryEngine::parallaxDetectionLimit_mas(scope1m, 12);
    test::check(limit12 < limit6, "more epochs -> smaller parallax limit");
    test::check(limit6 > 0.0, "parallax limit is positive");

    // -----------------------------------------------------------------------
    // canMeasureParallax
    // -----------------------------------------------------------------------
    StarCatalog cat = StarCatalog::loadBuiltin();

    // Barnard's Star: parallax ~546 mas, should be measurable
    const Star* barnard = cat.findByName("Barnard's Star");
    if (barnard) {
        test::check(DiscoveryEngine::canMeasureParallax(*barnard, scope1m, 6),
                    "Barnard's Star parallax measurable with 1m scope");
    }

    // Deneb: ~1.2 mas parallax, very hard to measure
    const Star* deneb = cat.findByName("Deneb");
    if (deneb) {
        bool measurable = DiscoveryEngine::canMeasureParallax(*deneb, scope1m, 6);
        // Deneb's parallax is ~1.2 mas; typical limit for 1m is ~14 mas
        // So Deneb should NOT be measurable
        test::check(!measurable,
                    "Deneb parallax not measurable with 1m scope (too far)");
    }

    // -----------------------------------------------------------------------
    // Discovery recording workflow
    // -----------------------------------------------------------------------
    DiscoveryEngine engine;
    std::size_t idx = engine.newDiscovery(12345, "SN 2024abc",
                                           DiscoveryType::Supernova, 2460000.0);
    auto& disc = const_cast<Discovery&>(engine.discoveries()[idx]);

    // Not confirmed yet
    test::check(!engine.isConfirmed(disc), "discovery not confirmed initially");
    test::check(disc.n_confirmations == 0, "no confirmations initially");

    // Add three high-SNR observations -> should be confirmed
    for (int i = 0; i < 3; ++i) {
        Observation obs;
        obs.jd         = 2460000.0 + i;
        obs.snr        = 10.0; // >= kDiscoverySnrThreshold (7.0)
        obs.v_magnitude= 14.0;
        obs.exposure_s = 60.0;
        engine.recordObservation(disc, obs);
    }
    test::check(engine.isConfirmed(disc), "discovery confirmed after 3 high-SNR obs");
    test::check(disc.confirmed, "disc.confirmed flag set");
    test::check(disc.n_confirmations >= 3, "3 confirmations recorded");

    // -----------------------------------------------------------------------
    // Low-SNR observation: not a confirmation
    // -----------------------------------------------------------------------
    DiscoveryEngine engine2;
    std::size_t idx2 = engine2.newDiscovery(9999, "weak source",
                                             DiscoveryType::DirectDetection,
                                             2460001.0);
    auto& disc2 = const_cast<Discovery&>(engine2.discoveries()[idx2]);
    Observation low_snr_obs;
    low_snr_obs.snr = 2.0; // below threshold
    bool was_confirmation = engine2.recordObservation(disc2, low_snr_obs);
    test::check(!was_confirmation, "low-SNR obs not a confirmation");
    test::check(!engine2.isConfirmed(disc2), "still not confirmed");
    test::check(!disc2.observations.empty(), "observation still recorded");

    // -----------------------------------------------------------------------
    // discoveryTypeName
    // -----------------------------------------------------------------------
    test::check(std::string(discoveryTypeName(DiscoveryType::TransitMethod))
                == "Transit Method", "discoveryTypeName TransitMethod");
    test::check(std::string(discoveryTypeName(DiscoveryType::Supernova))
                == "Supernova", "discoveryTypeName Supernova");

    return 0;
}
