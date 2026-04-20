#pragma once

/// @file splitmix64.hpp
/// @brief SplitMix64 pseudo-random number generator.
///
/// Public-domain implementation by Sebastiano Vigna.
/// Reference: https://xoshiro.di.unimi.it/splitmix64.c
/// Used for cell-seed derivation from (master_seed, pixel_id) pairs.

#include <cstdint>

namespace parallax::universe::rng
{

/// @brief SplitMix64 — fast, non-cryptographic 64-bit PRNG with a single 64-bit state.
///
/// Suitable for seeding derivation: given a fixed input, always produces the same output.
/// NOT suitable for high-dimensional sampling (use xoshiro256** for that).
struct SplitMix64
{
    std::uint64_t state{0};

    /// @brief Construct from an explicit seed value.
    explicit constexpr SplitMix64(std::uint64_t seed) noexcept
        : state{seed}
    {
    }

    /// @brief Advance the state and return the next 64-bit value.
    constexpr std::uint64_t next() noexcept
    {
        std::uint64_t z = (state += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
};

/// @brief Convenience: run one SplitMix64 step from @p seed and return result.
///
/// Stateless helper — useful for deriving cell seeds without constructing a SplitMix64 object.
[[nodiscard]] constexpr std::uint64_t splitmix64(std::uint64_t seed) noexcept
{
    std::uint64_t z = (seed + UINT64_C(0x9e3779b97f4a7c15));
    z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
    return z ^ (z >> 31);
}

} // namespace parallax::universe::rng
