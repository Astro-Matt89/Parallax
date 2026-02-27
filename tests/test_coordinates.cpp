// tests/test_coordinates.cpp
#include "test_main.hpp"
#include "core/math/coordinates.hpp"
#include "core/math/vec3.hpp"
#include <cmath>

using namespace parallax;

int testCoordinates() {
    // -----------------------------------------------------------------------
    // Vec3 basic arithmetic
    // -----------------------------------------------------------------------
    Vec3 a{1.0, 2.0, 3.0};
    Vec3 b{4.0, 5.0, 6.0};

    test::checkNear((a + b).x, 5.0, 1e-12, "Vec3 add x");
    test::checkNear((a + b).y, 7.0, 1e-12, "Vec3 add y");
    test::checkNear((a - b).z, -3.0, 1e-12, "Vec3 sub z");
    test::checkNear(a.dot(b), 32.0, 1e-12, "Vec3 dot");

    Vec3 c = a.cross(b);
    test::checkNear(c.x, -3.0, 1e-12, "Vec3 cross x");
    test::checkNear(c.y,  6.0, 1e-12, "Vec3 cross y");
    test::checkNear(c.z, -3.0, 1e-12, "Vec3 cross z");

    Vec3 unit{3.0, 4.0, 0.0};
    test::checkNear(unit.length(), 5.0, 1e-12, "Vec3 length");
    Vec3 n = unit.normalized();
    test::checkNear(n.length(), 1.0, 1e-12, "Vec3 normalized length");

    // -----------------------------------------------------------------------
    // normDeg
    // -----------------------------------------------------------------------
    test::checkNear(normDeg(370.0), 10.0, 1e-10, "normDeg 370");
    test::checkNear(normDeg(-10.0), 350.0, 1e-10, "normDeg -10");
    test::checkNear(normDeg(0.0),     0.0, 1e-10, "normDeg 0");
    test::checkNear(normDeg(360.0),   0.0, 1e-10, "normDeg 360");

    // -----------------------------------------------------------------------
    // Airmass
    // -----------------------------------------------------------------------
    // At zenith (90 deg) airmass should be ≈ 1
    test::checkNear(airmass(90.0), 1.0, 0.01, "airmass zenith");
    // At 30 deg altitude airmass ≈ 2
    test::check(airmass(30.0) > 1.8 && airmass(30.0) < 2.2, "airmass 30 deg");
    // At 0 deg airmass should be clamped to 40
    test::checkNear(airmass(0.0), 40.0, 1e-10, "airmass horizon");

    // -----------------------------------------------------------------------
    // Angular separation: zero separation for identical points
    // -----------------------------------------------------------------------
    Equatorial p1{45.0, 30.0};
    test::checkNear(angularSeparation(p1, p1), 0.0, 1e-10, "angSep identical");

    // Known: separation from (0,0) to (0,90) should be 90 deg
    Equatorial pole{0.0, 90.0};
    Equatorial equator{0.0, 0.0};
    test::checkNear(angularSeparation(pole, equator), 90.0, 1e-8,
                    "angSep pole-equator");

    // -----------------------------------------------------------------------
    // equatorialToHorizontal: object on meridian (HA=0) should transit
    // at altitude = 90 - |lat - dec|
    // -----------------------------------------------------------------------
    Equatorial target{0.0, 30.0};
    double lat = 51.5;
    // On meridian: HA = 0
    Horizontal hor = equatorialToHorizontal(target, 0.0, lat);
    double expected_alt = 90.0 - std::abs(lat - target.dec_deg);
    test::checkNear(hor.alt_deg, expected_alt, 0.1, "equToHor transit alt");

    return 0;
}
