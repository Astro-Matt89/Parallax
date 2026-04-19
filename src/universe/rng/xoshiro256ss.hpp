#pragma once

/// @file xoshiro256ss.hpp
/// @brief xoshiro256** pseudo-random number generator.
///
/// Public-domain implementation by David Blackman and Sebastiano Vigna.
/// Reference: https://xoshiro.di.unimi.it/xoshiro256starstar.c
/// Used for per-cell and per-star sampling in the procedural engine.

#include "universe/rng/splitmix64.hpp"

#include <cstdint>

namespace parallax::universe::rng
{

/// @brief xoshiro256** — high-quality, fast 64-bit PRNG with a 256-bit state.
///
/// Suitable for per-cell and per-star sampling. Passes all known statistical tests.
/// Seeded via SplitMix64 to guarantee a non-zero starting state.
struct Xoshiro256ss
{
    std::uint64_t s[4]{0, 0, 0, 0};

    /// @brief Construct from a 64-bit seed; uses SplitMix64 to expand the seed.
    explicit Xoshiro256ss(std::uint64_t seed) noexcept
    {
        SplitMix64 sm{seed};
        s[0] = sm.next();
        s[1] = sm.next();
        s[2] = sm.next();
        s[3] = sm.next();
    }

    /// @brief Advance the state and return the next 64-bit value.
    std::uint64_t next() noexcept
    {
        const std::uint64_t result = rotl(s[1] * 5, 7) * 9;
        const std::uint64_t t = s[1] << 17;

        s[2] ^= s[0];
        s[3] ^= s[1];
        s[1] ^= s[2];
        s[0] ^= s[3];

        s[2] ^= t;
        s[3] = rotl(s[3], 45);

        return result;
    }

    /// @brief Return a uniform float in [0, 1).
    float next_float() noexcept
    {
        // Use upper 23 mantissa bits — avoids sub-normals.
        const std::uint32_t bits = static_cast<std::uint32_t>(next() >> 41);
        // 2^(-23) = 1.192093e-7
        return static_cast<float>(bits) * (1.0f / static_cast<float>(1u << 23));
    }

    /// @brief Return a uniform double in [0, 1).
    double next_double() noexcept
    {
        // Use upper 53 mantissa bits.
        const std::uint64_t bits = next() >> 11;
        return static_cast<double>(bits) * (1.0 / static_cast<double>(UINT64_C(1) << 53));
    }

private:
    [[nodiscard]] static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept
    {
        return (x << k) | (x >> (64 - k));
    }
};

} // namespace parallax::universe::rng
