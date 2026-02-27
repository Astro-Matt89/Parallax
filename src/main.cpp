// src/main.cpp - Parallax Observatory Simulator entry point
//
// Demonstrates the core simulation loop:
//  1. Load real star catalog
//  2. Generate procedural field
//  3. Set up observatory and instruments
//  4. Simulate an observing session
//  5. Render results to terminal

#include "core/math/coordinates.hpp"
#include "universe/star_catalog.hpp"
#include "universe/procedural_generator.hpp"
#include "observatory/observer.hpp"
#include "observatory/telescope.hpp"
#include "observatory/atmosphere.hpp"
#include "rendering/renderer.hpp"
#include "rendering/star_field.hpp"
#include "discovery/discovery_engine.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>

using namespace parallax;

int main() {
    std::cout << "================================================================\n"
              << "  PARALLAX v0.1 - Ground-Based Astronomical Observatory Sim\n"
              << "================================================================\n\n";

    // -----------------------------------------------------------------------
    // 1. Load built-in bright star catalog
    // -----------------------------------------------------------------------
    std::cout << "Loading star catalog...\n";
    StarCatalog catalog = StarCatalog::loadBuiltin();
    std::cout << "  Loaded " << catalog.size() << " catalog stars.\n\n";

    // -----------------------------------------------------------------------
    // 2. Generate procedural stars around Orion nebula region
    // -----------------------------------------------------------------------
    std::cout << "Generating procedural starfield (Orion region)...\n";
    constexpr uint64_t kUniverseSeed = 0xDEADBEEFCAFEBABEULL;
    ProceduralGenerator gen(kUniverseSeed, /*mag_limit=*/12.0);

    // Orion nebula centre: RA~83.8, Dec~-5.4
    constexpr double kOrionRA  = 83.8;
    constexpr double kOrionDec = -5.4;

    auto proc_stars = gen.generateTile(kOrionRA - 2.0, kOrionDec - 2.0,
                                       /*tile_size_deg=*/4.0);
    std::cout << "  Generated " << proc_stars.size()
              << " procedural stars (V<12).\n\n";

    // Add procedural stars to catalog for this session
    for (auto& s : proc_stars) catalog.addStar(s);

    // -----------------------------------------------------------------------
    // 3. Set up observatory, telescope, and session
    // -----------------------------------------------------------------------
    ObservingSite site = makeMaunaKeaSite();
    Telescope     scope= make1mReflector();

    // Start time: 2024-11-15 18:00 UTC (evening, Orion rising)
    double jd_start = julianDate(2024, 11, 15, 18.0);
    ObservingSession session(site, scope, jd_start);

    std::cout << "Observatory: " << site.name << "\n"
              << "  Lat: " << std::fixed << std::setprecision(4)
              << site.location.lat_deg << " deg N\n"
              << "  Lon: " << site.location.lon_deg << " deg E\n"
              << "  Elevation: " << site.location.elevation_m << " m\n\n";

    std::cout << "Telescope: " << scope.name << "\n"
              << "  Aperture: " << scope.aperture_mm << " mm\n"
              << "  F-ratio:  f/" << std::setprecision(1) << scope.fRatio() << "\n"
              << "  Pixel scale: " << std::setprecision(3)
              << scope.pixelScale() << " arcsec/pixel\n";
    auto [fov_w, fov_h] = scope.fieldOfView();
    std::cout << "  FOV: " << std::setprecision(3) << fov_w
              << " x " << fov_h << " degrees\n"
              << "  Diff. limit: " << std::setprecision(3)
              << scope.diffractionLimit_arcsec() << " arcsec\n\n";

    // -----------------------------------------------------------------------
    // 4. Query and render Orion nebula region
    // -----------------------------------------------------------------------
    Equatorial orion_centre{kOrionRA, kOrionDec};
    double query_radius = std::max(fov_w, fov_h) * 0.6;
    auto visible_stars = catalog.query(orion_centre, query_radius, /*mag_limit=*/11.0);

    std::cout << "Query: " << visible_stars.size()
              << " stars within " << std::setprecision(2) << query_radius
              << " deg of Orion Nebula (V<11)\n\n";

    // Check if Betelgeuse is above horizon
    const Star* betelgeuse = catalog.findByName("Betelgeuse");
    if (betelgeuse) {
        bool visible = session.isVisible(betelgeuse->position);
        double snr_60 = session.snr(betelgeuse->position,
                                    betelgeuse->v_magnitude, 60.0);
        std::cout << "Betelgeuse status:\n"
                  << "  Above 15 deg altitude: " << (visible ? "Yes" : "No") << "\n"
                  << "  SNR (60s exposure):     "
                  << std::setprecision(1) << snr_60 << "\n\n";
    }

    // -----------------------------------------------------------------------
    // 5. Render terminal starfield
    // -----------------------------------------------------------------------
    AtmosphericModel atm(site.conditions);
    Horizontal orion_hor = session.toHorizontal(orion_centre);

    auto star_records = StarField::build(
        visible_stars, scope, atm,
        orion_centre,
        fov_w * 2.0, fov_h * 2.0,
        /*image_w=*/76, /*image_h=*/22,
        orion_hor.alt_deg);

    ConsoleRenderer renderer;
    renderer.renderStarField(std::cout, star_records,
                              "Orion Nebula Region | V<11 | 1-m Reflector");

    // -----------------------------------------------------------------------
    // 6. Print instrument status
    // -----------------------------------------------------------------------
    double orion_snr = session.snr(orion_centre, 4.0, /*exposure_s=*/120.0);
    renderer.renderStatusPanel(std::cout, session, orion_centre, orion_snr);

    // -----------------------------------------------------------------------
    // 7. Star readout for Betelgeuse
    // -----------------------------------------------------------------------
    if (betelgeuse) {
        renderer.renderStarReadout(std::cout, *betelgeuse, session, 60.0);
    }

    // -----------------------------------------------------------------------
    // 8. Discovery mechanics demo: parallax measurement
    // -----------------------------------------------------------------------
    std::cout << "\n--- Discovery Mechanics Demo ---\n";
    const Star* barnard = catalog.findByName("Barnard's Star");
    if (barnard) {
        bool measurable = DiscoveryEngine::canMeasureParallax(*barnard, scope, 6);
        double limit = DiscoveryEngine::parallaxDetectionLimit_mas(scope, 6);
        std::cout << "Barnard's Star parallax: "
                  << barnard->parallax_mas << " mas\n"
                  << "Parallax detection limit (1m, 6 epochs): "
                  << std::setprecision(3) << limit << " mas\n"
                  << "Measurable: " << (measurable ? "Yes" : "No") << "\n\n";

        // Transit detection demo
        double planet_r = DiscoveryEngine::minimumDetectablePlanetRadius(
            session.snr(barnard->position, barnard->v_magnitude, 300.0),
            /*star_radius_rs=*/0.2 // M-dwarf
        );
        std::cout << "Min detectable planet radius (300s): "
                  << std::setprecision(2) << planet_r << " Earth radii\n\n";
    }

    // -----------------------------------------------------------------------
    // 9. Procedural generation consistency check
    // -----------------------------------------------------------------------
    std::cout << "Determinism check: regenerating same tile...\n";
    auto proc2 = gen.generateTile(kOrionRA - 2.0, kOrionDec - 2.0, 4.0);
    bool same = (proc2.size() == proc_stars.size());
    if (same && !proc2.empty()) {
        same = (proc2[0].id == proc_stars[0].id) &&
               (proc2[0].v_magnitude == proc_stars[0].v_magnitude);
    }
    std::cout << "  Same tile, same seed -> identical output: "
              << (same ? "PASS" : "FAIL") << "\n\n";

    std::cout << "================================================================\n"
              << "  Session complete. Clear skies.\n"
              << "================================================================\n";
    return 0;
}
