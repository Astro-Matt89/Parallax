/// @file test_mulberry32.cpp
/// @brief Unit tests for the mulberry32 PRNG (Sprint 10b Task 10b.2).
///
/// The mulberry32 generator must be BIT-EXACT with the sandbox JS oracle.
/// Every expected value below was computed by running the JS algorithm directly:
///
///   function imul32(a,b){ return (Math.imul(a,b)>>>0); }
///   function step(state){
///     state = (state + 0x6D2B79F5) >>> 0;
///     let t = imul32(state ^ (state >>> 15), 1 | state);
///     t = (t + imul32(t ^ (t>>>7), 61|t)) ^ t; t >>>= 0;
///     return [(t^(t>>>14))>>>0, state];
///   }
///   // then result = raw / 4294967296
///
/// Any deviation from these values indicates a port error.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "interferometry/mulberry32.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using parallax::interferometry::Mulberry32;

    // Tolerance for double equality (bit-exact port should produce identical doubles).
    constexpr double kExact = 1.0e-15;

    // Reference values computed from the JS oracle for seed=0.
    // Format: {expected_output_from_next()}
    // Verified by running the JS function with seed=0 and collecting outputs.
    constexpr double kSeed0Expected[] = {
        0.26642920868471265,  // draw 0
        0.0003297457005828619, // draw 1
        0.2232720274478197,   // draw 2
        0.1462021479383111,   // draw 3
        0.46732782293111086,  // draw 4
        0.5450490827206522,   // draw 5
        0.6152513844426721,   // draw 6
        0.6489853798411787,   // draw 7
        0.45600721263326705,  // draw 8
        0.581218967679888,    // draw 9
        0.24625753005966544,  // draw 10
        0.09785169479437172,  // draw 11
    };

    // Reference values for seed=42.
    constexpr double kSeed42Expected[] = {
        0.6011037519201636,
        0.44829055899754167,
        0.8524657934904099,
        0.6697340414393693,
        0.17481389874592423,
        0.5265925421845168,
        0.2732279943302274,
        0.6247446539346129,
        0.8654746483080089,
        0.4723170551005751,
        0.24992373422719538,
        0.8820588334929198,
    };
}

TEST_CASE("mulberry32 seed=0 matches JS oracle sequence (bit-exact)")
{
    Mulberry32 rng(0u);
    for (int i = 0; i < 12; ++i)
    {
        const double got = rng.next();
        CHECK_MESSAGE(std::abs(got - kSeed0Expected[i]) <= kExact,
            "draw", i, ": expected", kSeed0Expected[i], "got", got);
    }
}

TEST_CASE("mulberry32 seed=42 matches JS oracle sequence (bit-exact)")
{
    Mulberry32 rng(42u);
    for (int i = 0; i < 12; ++i)
    {
        const double got = rng.next();
        CHECK_MESSAGE(std::abs(got - kSeed42Expected[i]) <= kExact,
            "draw", i, ": expected", kSeed42Expected[i], "got", got);
    }
}

TEST_CASE("mulberry32 determinism: same seed produces same sequence")
{
    Mulberry32 rng1(12345u);
    Mulberry32 rng2(12345u);
    for (int i = 0; i < 50; ++i)
    {
        const double v1 = rng1.next();
        const double v2 = rng2.next();
        CHECK(v1 == v2); // must be bit-for-bit identical
    }
}

TEST_CASE("mulberry32 different seeds diverge immediately")
{
    Mulberry32 rng1(0u);
    Mulberry32 rng2(1u);
    // First draws must differ (the state update involves the seed value).
    const double v1 = rng1.next();
    const double v2 = rng2.next();
    CHECK(v1 != v2);
}

TEST_CASE("mulberry32 all outputs are in [0, 1)")
{
    Mulberry32 rng(999u);
    for (int i = 0; i < 10000; ++i)
    {
        const double v = rng.next();
        CHECK(v >= 0.0);
        CHECK(v < 1.0);
    }
}

TEST_CASE("mulberry32 wraparound near 0xFFFFFFFF behaves correctly")
{
    // Seed near the uint32 maximum — the first state update wraps around.
    // Expected values computed from the JS oracle with seed=0xFFFFFFFF.
    constexpr double kNearMaxExpected[] = {
        0.8964226141106337,
        0.189478256739676,
        0.7156526781618595,
        0.9440599093213677,
        0.8452364315744489,
        0.5391399988438934,
    };

    Mulberry32 rng(0xFFFFFFFFu);
    for (int i = 0; i < 6; ++i)
    {
        const double got = rng.next();
        CHECK_MESSAGE(std::abs(got - kNearMaxExpected[i]) <= kExact,
            "draw", i, ": expected", kNearMaxExpected[i], "got", got);
    }
}

TEST_CASE("mulberry32 randn() non-caching Box-Muller consumes exactly 2 draws each call")
{
    // Verify by interleaving randn and next calls and checking state consistency.
    // Three separate generators, same seed.
    Mulberry32 ref(0u);
    Mulberry32 via_randn(0u);
    Mulberry32 verify(0u);

    // ref: consume 6 draws via next()
    const double r0 = ref.next(); // draw 0
    const double r1 = ref.next(); // draw 1
    const double r2 = ref.next(); // draw 2
    const double r3 = ref.next(); // draw 3
    const double r4 = ref.next(); // draw 4
    const double r5 = ref.next(); // draw 5

    // via_randn: consume same 6 draws as 3 randn() calls.
    const double z0 = via_randn.randn(); // draws 0 and 1
    const double z1 = via_randn.randn(); // draws 2 and 3
    const double z2 = via_randn.randn(); // draws 4 and 5

    // Verify the Box-Muller formula manually for the first call.
    const double safe_r0 = (r0 > 0.0) ? r0 : 1.0e-300;
    const double expected_z0 = std::sqrt(-2.0 * std::log(safe_r0))
        * std::cos(2.0 * parallax::astro_constants::kPi * r1);
    CHECK(std::abs(z0 - expected_z0) <= kExact);

    // Verify second and third calls similarly.
    const double safe_r2 = (r2 > 0.0) ? r2 : 1.0e-300;
    const double expected_z1 = std::sqrt(-2.0 * std::log(safe_r2))
        * std::cos(2.0 * parallax::astro_constants::kPi * r3);
    CHECK(std::abs(z1 - expected_z1) <= kExact);

    const double safe_r4 = (r4 > 0.0) ? r4 : 1.0e-300;
    const double expected_z2 = std::sqrt(-2.0 * std::log(safe_r4))
        * std::cos(2.0 * parallax::astro_constants::kPi * r5);
    CHECK(std::abs(z2 - expected_z2) <= kExact);

    // After 3 randn() (= 6 draws), the 7th draw from via_randn must equal
    // the 7th draw from ref (state was advanced identically).
    const double next_ref   = ref.next();        // draw 6
    const double next_randn = via_randn.next();  // draw 6
    CHECK(next_ref == next_randn); // bit-for-bit identical

    // Suppress unused-variable warnings.
    (void)r4; (void)r5; (void)z2;
    (void)verify;
}

TEST_CASE("mulberry32 randn() output is Gaussian (mean≈0, std≈1 over large sample)")
{
    Mulberry32 rng(7u);
    double sum = 0.0;
    double sum_sq = 0.0;
    constexpr int kN = 100000;
    for (int i = 0; i < kN; ++i)
    {
        const double z = rng.randn();
        sum += z;
        sum_sq += z * z;
    }
    const double mean = sum / kN;
    const double var  = sum_sq / kN - mean * mean;
    CHECK(std::abs(mean) < 0.01);           // mean ≈ 0
    CHECK(std::abs(std::sqrt(var) - 1.0) < 0.02); // std ≈ 1
}
