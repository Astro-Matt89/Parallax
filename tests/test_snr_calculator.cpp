/// @file test_snr_calculator.cpp
/// @brief Unit tests for the physical SNR model (Sprint 10a Task 10a.2).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "instruments/snr_calculator.hpp"

#include <cmath>

namespace
{
    using parallax::instruments::SNRCalculator;
    using parallax::instruments::SNRParameters;

    /// A reasonable baseline set of parameters for a moderately bright source.
    SNRParameters make_baseline()
    {
        return SNRParameters{
            .target_flux_jy = 1.0e-6,
            .collecting_area_m2 = 100.0,
            .efficiency = 0.8,
            .bandwidth_hz = 1.0e9,
            .integration_time_s = 100.0,
            .system_temperature_k = 50.0,
            .sky_background = 1.0,
            .num_stations = 1,
        };
    }
}

TEST_CASE("magnitude_to_flux_jy uses the AB zero point")
{
    // mag 0 -> 3631 Jy.
    CHECK(SNRCalculator::magnitude_to_flux_jy(0.0, 550.0) == doctest::Approx(3631.0));

    // Each 5 magnitudes is a factor of 100 in flux: mag 5 -> 36.31 Jy.
    CHECK(SNRCalculator::magnitude_to_flux_jy(5.0, 550.0) == doctest::Approx(36.31));

    // mag 10 -> 0.3631 Jy (another factor of 100).
    CHECK(SNRCalculator::magnitude_to_flux_jy(10.0, 550.0) == doctest::Approx(0.3631));
}

TEST_CASE("brighter target reaches a given SNR faster than a fainter one")
{
    SNRParameters bright = make_baseline();
    bright.target_flux_jy = SNRCalculator::magnitude_to_flux_jy(5.0, 550.0);

    SNRParameters faint = bright;
    faint.target_flux_jy = SNRCalculator::magnitude_to_flux_jy(15.0, 550.0);

    const double t_bright = SNRCalculator::time_to_reach_snr(30.0, bright);
    const double t_faint = SNRCalculator::time_to_reach_snr(30.0, faint);

    CHECK(t_bright < t_faint);
}

TEST_CASE("doubling integration time increases SNR by sqrt(2) in the Poisson regime")
{
    // Use a strong source and negligible read-noise contribution so the
    // background/source-limited (Poisson) regime dominates.
    SNRParameters params = make_baseline();
    params.target_flux_jy = 1.0e-3;
    params.integration_time_s = 1.0e4;

    const double snr_t = SNRCalculator::compute_snr(params);
    const double snr_2t = SNRCalculator::compute_snr_cumulative(params, 2.0e4);

    CHECK(snr_2t / snr_t == doctest::Approx(std::sqrt(2.0)).epsilon(0.01));
}

TEST_CASE("more active stations increase SNR (noise averaging)")
{
    SNRParameters one = make_baseline();
    one.num_stations = 1;

    SNRParameters four = one;
    four.num_stations = 4;

    const double snr_one = SNRCalculator::compute_snr(one);
    const double snr_four = SNRCalculator::compute_snr(four);

    CHECK(snr_four > snr_one);
    // sqrt(N) averaging: 4 stations -> ~2x SNR (Poisson-dominated regime).
    CHECK(snr_four / snr_one == doctest::Approx(2.0).epsilon(0.05));
}

TEST_CASE("larger combined aperture increases SNR")
{
    SNRParameters small = make_baseline();
    small.collecting_area_m2 = 100.0;

    SNRParameters large = small;
    large.collecting_area_m2 = 400.0;

    CHECK(SNRCalculator::compute_snr(large) > SNRCalculator::compute_snr(small));
}

TEST_CASE("zero exposure or zero signal yields zero SNR")
{
    SNRParameters no_time = make_baseline();
    no_time.integration_time_s = 0.0;
    CHECK(SNRCalculator::compute_snr(no_time) == doctest::Approx(0.0));

    SNRParameters no_signal = make_baseline();
    no_signal.target_flux_jy = 0.0;
    CHECK(SNRCalculator::compute_snr(no_signal) == doctest::Approx(0.0));
}
