#pragma once

// Bit-exact C++ port of the sandbox oracle mulberry32 generator.
//
// JS source (glasswing-sandbox-v1_7.html, SPECIFICA_10b_glasswing.md §2):
//
//   function mulberry32(a){return function(){
//     a|=0; a=a+0x6D2B79F5|0;
//     let t=Math.imul(a^a>>>15,1|a);
//     t=t+Math.imul(t^t>>>7,61|t)^t;
//     return ((t^t>>>14)>>>0)/4294967296;
//   }}
//
// JS semantics mapped to C++:
//   |0       : int32 coercion — identical bit pattern to uint32 modular arithmetic
//   >>>      : unsigned right shift — same as >> on uint32_t in C++
//   Math.imul: low 32 bits of 32-bit multiply — same as uint32_t * uint32_t
//   >>>0     : force unsigned 32-bit — uint32_t is always unsigned
//
// THIS IS A BINDING CONTRACT.  Changing this file invalidates every sandbox
// fixture.  Any deliberate deviation must be documented here and in the PR.
//
// Draw-consumption contract (binding — ties to fixture compatibility):
//   next()  : advances state once, returns one uniform double in [0, 1).
//   randn() : consumes exactly 2 draws (non-caching Box-Muller):
//               u1 = next();  u2 = next();
//               z0 = sqrt(-2 * ln(u1)) * cos(2π * u2);
//               returns z0 only; the second variate z1 is discarded.
//             If a fixture reveals that the sandbox caches z1, switch to the
//             caching form and update this comment.
//
// Seeding convention for station errors (SPECIFICA §2):
//   The Kolmogorov / gain / noise error generator is seeded with
//   atm_seed ^ 0x9e3779b9u — do NOT share this stream with the target-model RNG.

#include "core/types.hpp"

#include <cmath>
#include <cstdint>

namespace parallax::interferometry
{
    class Mulberry32
    {
    public:
        /// Initialise with a 32-bit seed.
        explicit Mulberry32(std::uint32_t seed) noexcept
            : m_state(seed)
        {
        }

        /// Advance the state and return a uniform double in [0, 1).
        [[nodiscard]] double next() noexcept
        {
            // JS: a|=0; a=a+0x6D2B79F5|0;
            m_state += 0x6D2B79F5u;

            // JS: let t=Math.imul(a^a>>>15,1|a);
            std::uint32_t t = (m_state ^ (m_state >> 15u)) * (1u | m_state);

            // JS: t=t+Math.imul(t^t>>>7,61|t)^t;
            t = (t + (t ^ (t >> 7u)) * (61u | t)) ^ t;

            // JS: return ((t^t>>>14)>>>0)/4294967296;
            return static_cast<double>(t ^ (t >> 14u)) / 4294967296.0;
        }

        /// Return a standard-normal variate via Box-Muller (non-caching, 2 draws).
        [[nodiscard]] double randn() noexcept
        {
            const double u1 = next();
            const double u2 = next();
            // Guard against log(0): next() can return 0.0 if the hash produces 0.
            const double safe_u1 = (u1 > 0.0) ? u1 : 1.0e-300;
            return std::sqrt(-2.0 * std::log(safe_u1))
                * std::cos(astro_constants::kTwoPi * u2);
        }

    private:
        std::uint32_t m_state;
    };
}
