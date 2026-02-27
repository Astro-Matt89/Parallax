// tests/test_telescope.cpp
#include "test_main.hpp"
#include "observatory/telescope.hpp"
#include <cmath>

using namespace parallax;

int testTelescope() {
    Telescope scope = make1mReflector();

    // -----------------------------------------------------------------------
    // Basic derived properties
    // -----------------------------------------------------------------------
    test::checkNear(scope.fRatio(), 8.0, 0.01, "1m reflector f/8");
    test::check(scope.pixelScale() > 0.0, "pixel scale positive");
    // For 13.5 um pixels at f/8000mm: 13.5/8000 * 206.265 ≈ 0.348 arcsec/px
    test::checkNear(scope.pixelScale(), 0.348, 0.01, "pixel scale 1m");

    auto [fw, fh] = scope.fieldOfView();
    test::check(fw > 0.0 && fh > 0.0, "FOV positive");
    test::check(fw < 1.0, "FOV less than 1 degree for 1m");

    // -----------------------------------------------------------------------
    // Diffraction limit
    // -----------------------------------------------------------------------
    double dl = scope.diffractionLimit_arcsec();
    // 1m aperture at 550nm → Rayleigh ~0.138 arcsec
    test::checkNear(dl, 0.138, 0.01, "1m diffraction limit");

    // -----------------------------------------------------------------------
    // Collecting area (with central obstruction)
    // -----------------------------------------------------------------------
    double area = scope.collectingArea_cm2();
    double full = std::numbers::pi / 4.0 * (100.0 * 100.0); // 1m in cm
    double blocked = std::numbers::pi / 4.0 * (20.0 * 20.0);  // 20% obstruction
    test::checkNear(area, full - blocked, 1.0, "collecting area with obstruction");

    // -----------------------------------------------------------------------
    // Photon flux: Vega (V=0) should give many photons
    // -----------------------------------------------------------------------
    double flux_vega = scope.photonFlux(0.0, 1.0);
    test::check(flux_vega > 1e6, "Vega flux > 1e6 photons/s on 1m");

    // Flux decreases for fainter stars (5 mag = 100x fainter)
    double flux_5 = scope.photonFlux(5.0, 1.0);
    test::checkNear(flux_vega / flux_5, 100.0, 5.0, "flux ratio 5 mag = 100x");

    // -----------------------------------------------------------------------
    // SNR: Sirius (-1.46) should have very high SNR
    // -----------------------------------------------------------------------
    double snr_sirius = scope.snr(-1.46, 10.0);
    test::check(snr_sirius > 100.0, "Sirius SNR > 100 on 1m");

    // A V=15 star should have much lower SNR
    double snr_faint = scope.snr(15.0, 60.0);
    test::check(snr_faint < snr_sirius, "faint star lower SNR");

    // -----------------------------------------------------------------------
    // Limiting magnitude
    // -----------------------------------------------------------------------
    double lim = scope.limitingMagnitude(/*exposure_s=*/300.0);
    test::check(lim > 18.0, "1m reflector limiting magnitude > 18 (300s)");
    test::check(lim < 30.0, "limiting magnitude < 30 (sanity)");

    // Longer exposure -> fainter limit
    double lim_30 = scope.limitingMagnitude(30.0);
    double lim_300 = scope.limitingMagnitude(300.0);
    test::check(lim_300 > lim_30, "longer exposure deeper limit");

    // -----------------------------------------------------------------------
    // 8" SCT sanity checks
    // -----------------------------------------------------------------------
    Telescope sct = makeSchCas8inch();
    test::checkNear(sct.fRatio(), 10.0, 0.05, "8in SCT f/10");
    test::check(sct.diffractionLimit_arcsec() > scope.diffractionLimit_arcsec(),
                "smaller scope has worse diffraction limit");
    test::check(sct.limitingMagnitude(300.0) < lim_300,
                "smaller scope shallower limit");

    return 0;
}
