#include "core/fft.hpp"

#include <cassert>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace parallax::core
{

// ── 1-D in-place radix-2 DIT FFT ─────────────────────────────────────────────

void fft1d(std::vector<std::complex<double>>& data, bool inverse)
{
    const std::uint32_t n = static_cast<std::uint32_t>(data.size());
    assert(is_power_of_two(n) && "fft1d: size must be a power of 2");

    // Bit-reversal permutation
    for (std::uint32_t i = 1u, j = 0u; i < n; ++i)
    {
        std::uint32_t bit = n >> 1u;
        for (; j & bit; bit >>= 1u)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    // Cooley-Tukey butterfly stages
    for (std::uint32_t len = 2u; len <= n; len <<= 1u)
    {
        const double theta = (inverse ? 1.0 : -1.0) * 2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(theta), std::sin(theta));

        for (std::uint32_t i = 0u; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (std::uint32_t k = 0u; k < len / 2u; ++k)
            {
                const std::complex<double> u = data[i + k];
                const std::complex<double> v = data[i + k + len / 2u] * w;
                data[i + k]             = u + v;
                data[i + k + len / 2u] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse)
    {
        const double inv_n = 1.0 / static_cast<double>(n);
        for (auto& c : data)
            c *= inv_n;
    }
}

// ── 2-D helpers ───────────────────────────────────────────────────────────────

static void apply_rows(std::vector<double>& re, std::vector<double>& im,
                       std::uint32_t N, bool inverse)
{
    std::vector<std::complex<double>> row(N);
    for (std::uint32_t r = 0u; r < N; ++r)
    {
        for (std::uint32_t c = 0u; c < N; ++c)
            row[c] = {re[r * N + c], im[r * N + c]};
        fft1d(row, inverse);
        for (std::uint32_t c = 0u; c < N; ++c)
        {
            re[r * N + c] = row[c].real();
            im[r * N + c] = row[c].imag();
        }
    }
}

static void apply_cols(std::vector<double>& re, std::vector<double>& im,
                       std::uint32_t N, bool inverse)
{
    std::vector<std::complex<double>> col(N);
    for (std::uint32_t c = 0u; c < N; ++c)
    {
        for (std::uint32_t r = 0u; r < N; ++r)
            col[r] = {re[r * N + c], im[r * N + c]};
        fft1d(col, inverse);
        for (std::uint32_t r = 0u; r < N; ++r)
        {
            re[r * N + c] = col[r].real();
            im[r * N + c] = col[r].imag();
        }
    }
}

void fft2(std::vector<double>& re, std::vector<double>& im, std::uint32_t N)
{
    assert(is_power_of_two(N) && "fft2: N must be a power of 2");
    assert(re.size() == static_cast<std::size_t>(N) * N);
    assert(im.size() == static_cast<std::size_t>(N) * N);

    apply_rows(re, im, N, /*inverse=*/false);
    apply_cols(re, im, N, /*inverse=*/false);
}

void ifft2(std::vector<double>& re, std::vector<double>& im, std::uint32_t N)
{
    assert(is_power_of_two(N) && "ifft2: N must be a power of 2");
    assert(re.size() == static_cast<std::size_t>(N) * N);
    assert(im.size() == static_cast<std::size_t>(N) * N);

    // Inverse row-column: inverse rows then inverse cols.
    // Each 1-D IFFT applies 1/N; combined effect is 1/N² for the 2-D grid.
    apply_rows(re, im, N, /*inverse=*/true);
    apply_cols(re, im, N, /*inverse=*/true);
}

// ── fftshift (quadrant swap) ──────────────────────────────────────────────────

void shift2(std::vector<double>& data, std::uint32_t N)
{
    assert(N % 2u == 0u && "shift2: N must be even");
    assert(data.size() == static_cast<std::size_t>(N) * N);

    const std::uint32_t half = N / 2u;

    // Swap quadrants Q0↔Q2 and Q1↔Q3
    for (std::uint32_t r = 0u; r < half; ++r)
    {
        for (std::uint32_t c = 0u; c < half; ++c)
        {
            // Q0 (top-left) ↔ Q2 (bottom-right)
            std::swap(data[r * N + c], data[(r + half) * N + (c + half)]);
            // Q1 (top-right) ↔ Q3 (bottom-left)
            std::swap(data[r * N + (c + half)], data[(r + half) * N + c]);
        }
    }
}

} // namespace parallax::core
