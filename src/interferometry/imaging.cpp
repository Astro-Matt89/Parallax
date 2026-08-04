#include "interferometry/imaging.hpp"

#include "core/fft.hpp"

#include <spdlog/spdlog.h>

#include <cassert>
#include <cmath>
#include <vector>

namespace parallax::interferometry
{

DirtyImages make_images(const std::vector<Visibility>& points,
                        double du,
                        std::uint32_t N,
                        Weighting weighting)
{
    assert(parallax::core::is_power_of_two(N) && "make_images: N must be a power of 2");

    const std::size_t sz = static_cast<std::size_t>(N) * N;

    // Accumulation grids: visibilities (gRe, gIm) and sampling weights (W).
    std::vector<double> gRe(sz, 0.0);
    std::vector<double> gIm(sz, 0.0);
    std::vector<double> W(sz, 0.0);

    for (const auto& vis : points)
    {
        // Map (u,v) to grid coordinates.  du = 1/theta_fov_rad (wavelengths/pixel).
        const double gx_f = static_cast<double>(N) / 2.0 + vis.u / du;
        const double gy_f = static_cast<double>(N) / 2.0 + vis.v / du;

        // Nearest-cell gridding.
        const auto gx = static_cast<std::int32_t>(std::round(gx_f));
        const auto gy = static_cast<std::int32_t>(std::round(gy_f));

        if (gx < 0 || gx >= static_cast<std::int32_t>(N) ||
            gy < 0 || gy >= static_cast<std::int32_t>(N))
            continue;

        const std::size_t idx = static_cast<std::size_t>(gy) * N + static_cast<std::size_t>(gx);

        // Hermitian symmetry: grid (u,v) and its conjugate (-u,-v).
        gRe[idx] += vis.Vr;
        gIm[idx] += vis.Vi;
        W[idx]   += 1.0;

        // Conjugate: (-u, -v) maps to (N - gx, N - gy) mod N.
        // Edge case: gx == 0 → conj_x = N (out of range), so use 0 with wrap.
        const auto cx = static_cast<std::uint32_t>((N - static_cast<std::uint32_t>(gx)) % N);
        const auto cy = static_cast<std::uint32_t>((N - static_cast<std::uint32_t>(gy)) % N);
        const std::size_t cidx = static_cast<std::size_t>(cy) * N + static_cast<std::size_t>(cx);

        gRe[cidx] += vis.Vr;
        gIm[cidx] -= vis.Vi;   // conjugate: negate imaginary part
        W[cidx]   += 1.0;
    }

    // Uniform weighting: divide each occupied cell by its weight.
    if (weighting == Weighting::Uniform)
    {
        for (std::size_t i = 0u; i < sz; ++i)
        {
            if (W[i] > 0.0)
            {
                gRe[i] /= W[i];
                gIm[i] /= W[i];
            }
        }
    }

    // Build dirty beam from weights.
    // beam_im is all-zero for real weights; kept for the IFFT call.
    std::vector<double> beam_re = W;
    std::vector<double> beam_im(sz, 0.0);

    // Dirty image from gridded visibilities.
    std::vector<double> dirty_re = gRe;
    std::vector<double> dirty_im = gIm;

    // IFFT2 (includes 1/N² normalisation).
    parallax::core::ifft2(beam_re,  beam_im,  N);
    parallax::core::ifft2(dirty_re, dirty_im, N);

    // fftshift: place DC (beam peak) at the centre (N/2, N/2).
    parallax::core::shift2(beam_re, N);
    parallax::core::shift2(dirty_re, N);

    // Normalise by beam peak.
    const double beam_peak = beam_re[static_cast<std::size_t>(N / 2u) * N + N / 2u];
    if (std::abs(beam_peak) < 1.0e-30)
    {
        spdlog::warn("make_images: beam peak is ~0 — no visibility data? Skipping normalisation.");
        return DirtyImages{std::move(beam_re), std::move(dirty_re), N, du};
    }

    for (auto& v : beam_re)
        v /= beam_peak;
    for (auto& v : dirty_re)
        v /= beam_peak;

    return DirtyImages{std::move(beam_re), std::move(dirty_re), N, du};
}

} // namespace parallax::interferometry
