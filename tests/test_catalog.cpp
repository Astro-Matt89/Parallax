// tests/test_catalog.cpp
#include "test_main.hpp"
#include "universe/star_catalog.hpp"
#include "universe/star.hpp"
#include <cmath>

using namespace parallax;

int testCatalog() {
    StarCatalog cat = StarCatalog::loadBuiltin();

    // -----------------------------------------------------------------------
    // Basic loading
    // -----------------------------------------------------------------------
    test::check(cat.size() > 0, "catalog non-empty");
    test::check(cat.size() >= 20, "catalog has at least 20 stars");

    // -----------------------------------------------------------------------
    // findByName (case-insensitive)
    // -----------------------------------------------------------------------
    const Star* sirius = cat.findByName("Sirius");
    test::check(sirius != nullptr, "findByName Sirius found");
    if (sirius) {
        test::checkNear(sirius->v_magnitude, -1.46, 0.01, "Sirius magnitude");
        test::checkNear(sirius->position.ra_deg,  101.287, 0.01, "Sirius RA");
        test::checkNear(sirius->position.dec_deg, -16.716, 0.01, "Sirius Dec");
        test::check(sirius->spectral_class == SpectralClass::A, "Sirius spectral A");
    }

    const Star* polaris = cat.findByName("polaris"); // lowercase
    test::check(polaris != nullptr, "findByName case insensitive Polaris");

    const Star* notexist = cat.findByName("Zaphod");
    test::check(notexist == nullptr, "findByName missing returns nullptr");

    // -----------------------------------------------------------------------
    // findById
    // -----------------------------------------------------------------------
    const Star* sirius2 = cat.findById(32349);
    test::check(sirius2 != nullptr, "findById Sirius");
    if (sirius2) {
        test::check(sirius2->name == "Sirius", "findById returns correct name");
    }
    test::check(cat.findById(999999999ULL) == nullptr, "findById missing returns nullptr");

    // -----------------------------------------------------------------------
    // Spatial query around Orion (RA~88, Dec~7) should find Betelgeuse
    // -----------------------------------------------------------------------
    Equatorial orion_centre{88.0, 7.0};
    auto results = cat.query(orion_centre, /*radius_deg=*/5.0);
    bool found_betelgeuse = false;
    for (const Star* s : results) {
        if (s->name == "Betelgeuse") found_betelgeuse = true;
    }
    test::check(found_betelgeuse, "query finds Betelgeuse near Orion");

    // -----------------------------------------------------------------------
    // Query with magnitude limit
    // -----------------------------------------------------------------------
    auto all   = cat.query(orion_centre, 30.0);
    auto bright= cat.query(orion_centre, 30.0, 2.0);
    test::check(bright.size() <= all.size(), "mag limit reduces results");

    // -----------------------------------------------------------------------
    // Distance modulus / parallax consistency
    // -----------------------------------------------------------------------
    const Star* vega = cat.findByName("Vega");
    if (vega && vega->distance_pc > 0.0) {
        double expected_plx = 1000.0 / vega->distance_pc;
        test::checkNear(vega->parallax_mas, expected_plx, 0.5, "Vega parallax");
    }

    // -----------------------------------------------------------------------
    // Colour derivation (smoke test)
    // -----------------------------------------------------------------------
    if (sirius) {
        Colour col = sirius->colour();
        test::check(col.r >= 0.f && col.r <= 1.f, "Sirius colour r in [0,1]");
        test::check(col.b >= 0.f && col.b <= 1.f, "Sirius colour b in [0,1]");
        // Sirius is A-type (blue-white) -> blue >= red
        test::check(col.b >= col.r, "Sirius colour blue >= red");
    }

    return 0;
}
