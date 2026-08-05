/// @file test_fft.cpp
/// @brief Unit tests for the radix-2 FFT utilities (Sprint 10b Task 10b.3).
///
/// Tests per SPECIFICA §6 level 1:
///   - Parseval's theorem (energy conservation)
///   - Impulse → flat spectrum
///   - Inverse(Forward(x)) == x within tolerance
///   - Hermitian input → real output
///   - fftshift (shift2) correctness

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/fft.hpp"

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace
{

constexpr double kTol = 1.0e-10;

/// Compute sum of squares of a complex vector (energy in spatial domain).
double energy(const std::vector<std::complex<double>>& v)
{
    double s = 0.0;
    for (const auto& c : v)
        s += std::norm(c);
    return s;
}

} // anonymous namespace

// ── fft1d ────────────────────────────────────────────────────────────────────

TEST_CASE("fft1d: impulse → flat spectrum")
{
    constexpr std::uint32_t N = 16u;
    std::vector<std::complex<double>> data(N, {0.0, 0.0});
    data[0] = {1.0, 0.0};  // impulse at index 0

    parallax::core::fft1d(data, false);

    // All bins must have magnitude N (un-normalised FFT of unit impulse = constant N).
    // Wait — an impulse at index 0 gives a flat spectrum with all bins == 1 (amplitude 1),
    // because the DFT of δ[0] is 1 for all k.
    for (std::uint32_t k = 0u; k < N; ++k)
    {
        CHECK(std::abs(data[k].real() - 1.0) < kTol);
        CHECK(std::abs(data[k].imag()) < kTol);
    }
}

TEST_CASE("fft1d: inverse(forward(x)) == x within tolerance")
{
    constexpr std::uint32_t N = 32u;
    // Build a non-trivial input.
    std::vector<std::complex<double>> original(N);
    for (std::uint32_t i = 0u; i < N; ++i)
        original[i] = {std::sin(2.0 * std::numbers::pi * 3.0 * i / N),
                        std::cos(2.0 * std::numbers::pi * 5.0 * i / N)};

    auto data = original;
    parallax::core::fft1d(data, false);  // forward
    parallax::core::fft1d(data, true);   // inverse

    for (std::uint32_t i = 0u; i < N; ++i)
    {
        CHECK(std::abs(data[i].real() - original[i].real()) < kTol);
        CHECK(std::abs(data[i].imag() - original[i].imag()) < kTol);
    }
}

TEST_CASE("fft1d: Parseval's theorem (energy conservation)")
{
    constexpr std::uint32_t N = 64u;
    std::vector<std::complex<double>> data(N);
    for (std::uint32_t i = 0u; i < N; ++i)
        data[i] = {static_cast<double>(i % 7) - 3.0, static_cast<double>(i % 5) - 2.0};

    const double E_before = energy(data);
    parallax::core::fft1d(data, false);
    const double E_after = energy(data);

    // Parseval: sum|X[k]|² == N × sum|x[n]|²  for un-normalised FFT.
    CHECK(std::abs(E_after - static_cast<double>(N) * E_before) <
          kTol * static_cast<double>(N) * E_before);
}

// ── fft2 / ifft2 ─────────────────────────────────────────────────────────────

TEST_CASE("fft2/ifft2: inverse(forward(x)) == x within tolerance")
{
    constexpr std::uint32_t N = 32u;
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    std::vector<double> re(sz), im(sz, 0.0);
    for (std::size_t i = 0u; i < sz; ++i)
        re[i] = std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / 100.0);

    const auto orig_re = re;
    const auto orig_im = im;

    parallax::core::fft2(re, im, N);
    parallax::core::ifft2(re, im, N);

    for (std::size_t i = 0u; i < sz; ++i)
    {
        CHECK(std::abs(re[i] - orig_re[i]) < kTol);
        CHECK(std::abs(im[i] - orig_im[i]) < kTol);
    }
}

TEST_CASE("fft2: 2-D impulse at (0,0) gives flat spectrum")
{
    constexpr std::uint32_t N = 16u;
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    std::vector<double> re(sz, 0.0), im(sz, 0.0);
    re[0] = 1.0;  // impulse at (0,0)

    parallax::core::fft2(re, im, N);

    for (std::size_t i = 0u; i < sz; ++i)
    {
        CHECK(std::abs(re[i] - 1.0) < kTol);
        CHECK(std::abs(im[i]) < kTol);
    }
}

TEST_CASE("fft2: Hermitian input → real output (imaginary part ~0 after IFFT)")
{
    // A real-valued input (im=0) has a Hermitian spectrum; IFFT of a Hermitian
    // input should yield a real output (im ≈ 0).
    constexpr std::uint32_t N = 16u;
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    // Create a Hermitian spectrum: W[k] = conj(W[N-k]).
    // The simplest Hermitian input is a real array with im=0.
    std::vector<double> re(sz), im(sz, 0.0);
    for (std::size_t i = 0u; i < sz; ++i)
        re[i] = static_cast<double>(i % 7) + 1.0;  // real-valued → Hermitian FFT output

    // Forward FFT of a real array → Hermitian spectrum.
    parallax::core::fft2(re, im, N);

    // Verify Hermitian symmetry: re[r,c] == re[N-r, N-c], im[r,c] == -im[N-r,N-c].
    for (std::uint32_t r = 1u; r < N; ++r)
    {
        for (std::uint32_t c = 1u; c < N; ++c)
        {
            const double re_fwd  = re[r * N + c];
            const double re_conj = re[(N - r) * N + (N - c)];
            const double im_fwd  = im[r * N + c];
            const double im_conj = im[(N - r) * N + (N - c)];
            CHECK(std::abs(re_fwd - re_conj) < kTol);
            CHECK(std::abs(im_fwd + im_conj) < kTol);
        }
    }

    // IFFT should recover a real output (im ≈ 0).
    parallax::core::ifft2(re, im, N);
    for (std::size_t i = 0u; i < sz; ++i)
        CHECK(std::abs(im[i]) < kTol);
}

// ── shift2 ────────────────────────────────────────────────────────────────────

TEST_CASE("shift2: DC moves to center after double application returns to origin")
{
    constexpr std::uint32_t N = 8u;
    const std::size_t sz = static_cast<std::size_t>(N) * N;

    std::vector<double> data(sz, 0.0);
    data[0] = 1.0;  // DC at (0,0)

    // After one shift, DC should be at (N/2, N/2).
    parallax::core::shift2(data, N);
    CHECK(data[(N / 2u) * N + N / 2u] == doctest::Approx(1.0).epsilon(kTol));
    CHECK(data[0] == doctest::Approx(0.0).epsilon(kTol));

    // After a second shift, DC returns to (0,0).
    parallax::core::shift2(data, N);
    CHECK(data[0] == doctest::Approx(1.0).epsilon(kTol));
    CHECK(data[(N / 2u) * N + N / 2u] == doctest::Approx(0.0).epsilon(kTol));
}

TEST_CASE("is_power_of_two: correct for powers and non-powers")
{
    using parallax::core::is_power_of_two;
    CHECK(is_power_of_two(1u));
    CHECK(is_power_of_two(2u));
    CHECK(is_power_of_two(4u));
    CHECK(is_power_of_two(16u));
    CHECK(is_power_of_two(128u));
    CHECK(!is_power_of_two(0u));
    CHECK(!is_power_of_two(3u));
    CHECK(!is_power_of_two(100u));
}
