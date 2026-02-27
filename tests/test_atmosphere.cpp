// tests/test_atmosphere.cpp
#include "test_main.hpp"
#include "observatory/atmosphere.hpp"
#include <cmath>

using namespace parallax;

int testAtmosphere() {
    AtmosphericConditions cond;
    cond.seeing_arcsec  = 2.0;
    cond.extinction_mag = 0.20;
    cond.bortle         = 4;
    cond.humidity_pct   = 40.0;
    cond.temperature_c  = 15.0;
    cond.pressure_hPa   = 1013.25;
    cond.transparency   = 1.0;

    AtmosphericModel atm(cond);

    // -----------------------------------------------------------------------
    // Bortle -> sky background
    // -----------------------------------------------------------------------
    test::checkNear(bortleToSkyBackground(1), 22.0, 1e-9, "Bortle 1 -> 22 mag");
    test::checkNear(bortleToSkyBackground(9), 16.5, 1e-9, "Bortle 9 -> 16.5 mag");

    // -----------------------------------------------------------------------
    // Fried parameter: r₀ from seeing
    // -----------------------------------------------------------------------
    // 2 arcsec seeing ≈ 0.98*500nm/r₀ => r₀ ≈ 6.4 cm
    double r0 = cond.friedParam_cm();
    test::check(r0 > 4.0 && r0 < 10.0, "r0 plausible range for 2\" seeing");

    // -----------------------------------------------------------------------
    // Airmass: zenith = 1, horizon = 40
    // -----------------------------------------------------------------------
    test::checkNear(AtmosphericModel::airmass(90.0), 1.0, 0.01, "airmass zenith");
    test::checkNear(AtmosphericModel::airmass(0.0), 40.0, 1e-10, "airmass horizon");

    // -----------------------------------------------------------------------
    // Extinction: at zenith should equal cond.extinction_mag
    // -----------------------------------------------------------------------
    test::checkNear(atm.extinction_mag(90.0), cond.extinction_mag, 0.001,
                    "extinction at zenith");
    // Should increase toward horizon (higher airmass)
    test::check(atm.extinction_mag(30.0) > atm.extinction_mag(90.0),
                "extinction increases toward horizon");

    // -----------------------------------------------------------------------
    // Seeing: effective seeing at 90 deg = base seeing
    // -----------------------------------------------------------------------
    test::checkNear(atm.effectiveSeeing_arcsec(90.0), cond.seeing_arcsec, 0.01,
                    "seeing at zenith");
    // Seeing should be worse at lower altitudes (higher airmass)
    test::check(atm.effectiveSeeing_arcsec(30.0) > cond.seeing_arcsec,
                "seeing degrades at lower altitude");

    // -----------------------------------------------------------------------
    // Refraction: at 90 deg altitude refraction should be near 0
    // -----------------------------------------------------------------------
    test::checkNear(atm.refraction_arcsec(90.0), 0.0, 0.01,
                    "refraction near zero at zenith");
    // At 45 deg ~ 58 arcsec refraction
    double ref45 = atm.refraction_arcsec(45.0);
    test::check(ref45 > 50.0 && ref45 < 70.0, "refraction at 45 deg plausible");

    // -----------------------------------------------------------------------
    // Sky background: darker overhead than at horizon
    // -----------------------------------------------------------------------
    test::check(atm.skyBackground(90.0) > atm.skyBackground(15.0),
                "sky darker overhead than near horizon");

    // -----------------------------------------------------------------------
    // apparentMagnitude: should be brighter (smaller number) for corrected
    // zenith star vs horizon (more extinction at horizon)
    // -----------------------------------------------------------------------
    test::check(atm.apparentMagnitude(5.0, 90.0) < atm.apparentMagnitude(5.0, 15.0),
                "object dimmer at horizon (more extinction)");

    return 0;
}
