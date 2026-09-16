#pragma once

/// @file closure.hpp
/// @brief Closure-phase computation for the Glasswing interferometric array.
///
/// ## Algorithm summary
///
/// For triangle (a, b, c) with station indices a < b < c, the closure phase is:
///
///   Φ = arg(V_ab) + arg(V_bc) − arg(V_ac)
///
/// where arg() = atan2(Vi, Vr), wrapped to the half-open interval (−π, π]
/// (SPECIFICA §3 normative formula).  This quantity is **immune to station-based
/// phase errors** (atmospheric turbulence, electronic phase drifts) because such
/// errors cancel when the three baselines are summed around the closed triangle.
/// Gain errors (real multiplicative scalars) do not affect any phase and therefore
/// also leave closure intact.  Thermal noise is NOT a station-based error; it adds
/// independently on each baseline and therefore DOES corrupt the closure phase.
///
/// ## Triangle selection rule — DELIBERATE divergence from the oracle
///
/// Candidate triangles (a, b, c) are enumerated in ascending station-index order
/// (a < b < c, then b increases before a increases).  The first `max_triangles`
/// candidates that have **at least one time sample where all three baselines
/// (a,b), (b,c), (a,c) are simultaneously present** in the supplied visibility
/// list are selected.
///
/// The oracle (`compute()` in tools/glasswing-sandbox-v1_8_0.html) instead takes the
/// first three triangles by index whether or not they have any data.  This port does
/// NOT follow it, on purpose: a triangle without common-time coverage yields an empty
/// series and carries no information, whereas skipping it makes `max_triangles` count
/// informative triangles only.  The two rules select the same triangles whenever the
/// lowest-index ones are covered.  No fixture exports closure phases, so the oracle
/// gate does not check this choice.  Decided in Sprint 10b — see SPECIFICA_10b §3.
///
/// ## HBT degeneracy
///
/// In `InstrumentMode::Hbt`, the sampler forces tVi = 0 for every sample, so
/// every individual phase is 0 or π, and the closure phase is also 0 or π.
/// This is **expected behaviour**, not a bug.  Intensity interferometry discards
/// phase information by construction; different reconstruction techniques are
/// required in that regime.
///
/// ## Wrapping convention
///
/// `wrap_phase(x)` maps any angle to the half-open interval (−π, π]:
///   - values in (−π, π] are returned unchanged
///   - values at exactly −π are mapped to +π (so −π is excluded)

#include "interferometry/ephemeris.hpp"
#include "interferometry/uv_sampling.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace parallax::interferometry
{
    // ── Data structures ───────────────────────────────────────────────────────────

    /// One closure-phase measurement at a single time sample for a fixed triangle.
    struct ClosureSample
    {
        std::uint32_t time_index; ///< Time-sample index (ascending order within the triangle).
        double observed;          ///< arg(V_ab)+arg(V_bc)−arg(V_ac) from corrupted visibilities, (−π,π].
        double truth;             ///< Same quantity from true (noiseless) visibilities, (−π,π].
    };

    /// Closure-phase time series for one station triangle.
    struct ClosureTriangle
    {
        std::uint32_t a;                     ///< First station index  (a < b < c).
        std::uint32_t b;                     ///< Second station index.
        std::uint32_t c;                     ///< Third station index.
        std::vector<ClosureSample> samples;  ///< Time series; ascending time_index.
    };

    // ── Helpers ───────────────────────────────────────────────────────────────────

    /// Wrap an angle (radians) to the half-open interval (−π, π].
    ///
    /// Exactly −π is mapped to +π; all other values in [−π, π] are unchanged.
    [[nodiscard]] double wrap_phase(double phi) noexcept;

    // ── Main API ──────────────────────────────────────────────────────────────────

    /// Compute closure phases for up to `max_triangles` station triangles.
    ///
    /// @param points        Visibility samples produced by `sample_uv`.
    ///                      Each sample must carry valid `station_i`, `station_j`,
    ///                      and `time_index` fields (`station_i < station_j` as
    ///                      emitted by `sample_uv`).
    /// @param stations      The station list used when calling `sample_uv`.
    ///                      Only the count (`stations.size()`) is used here; the
    ///                      geometric coordinates are not accessed.
    /// @param max_triangles Maximum number of triangles to return (default 3).
    ///                      Exposed rather than hardcoded to support diagnostics
    ///                      and future UI extensions (CLAUDE.md §9.1).
    ///
    /// @return Vector of `ClosureTriangle`, each containing a time series of
    ///         closure samples at times where all three baselines were observed.
    ///         Returns an empty vector (and logs at debug level) when there are
    ///         fewer than 3 stations, no visibility samples, or no triangle has
    ///         any time sample with full three-baseline coverage.
    [[nodiscard]] std::vector<ClosureTriangle> compute_closure_phases(
        const std::vector<Visibility>& points,
        const std::vector<Station>& stations,
        std::size_t max_triangles = 3);

} // namespace parallax::interferometry
