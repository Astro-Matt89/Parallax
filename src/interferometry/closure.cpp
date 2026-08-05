/// @file closure.cpp
/// @brief Closure-phase computation — see closure.hpp for algorithm documentation.

#include "interferometry/closure.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <unordered_map>
#include <vector>

namespace parallax::interferometry
{
    // ── Helpers ───────────────────────────────────────────────────────────────────

    double wrap_phase(double phi) noexcept
    {
        constexpr double kTwoPi = 2.0 * std::numbers::pi;
        // Bring into (-2π, 2π) first, then shift.
        phi = std::fmod(phi, kTwoPi);
        if (phi > std::numbers::pi)
            phi -= kTwoPi;
        else if (phi <= -std::numbers::pi)
            phi += kTwoPi;
        return phi;
    }

    // ── Main implementation ────────────────────────────────────────────────────────

    /// Key for the visibility index: (station_i, station_j, time_index).
    struct VisKey
    {
        std::uint32_t si;
        std::uint32_t sj;
        std::uint32_t tk;

        bool operator==(const VisKey& o) const noexcept
        {
            return si == o.si && sj == o.sj && tk == o.tk;
        }
    };

    struct VisKeyHash
    {
        std::size_t operator()(const VisKey& k) const noexcept
        {
            // Simple but collision-resistant combine for small integer values.
            std::size_t h = k.si;
            h = h * 1000003u + k.sj;
            h = h * 1000003u + k.tk;
            return h;
        }
    };

    std::vector<ClosureTriangle> compute_closure_phases(
        const std::vector<Visibility>& points,
        const std::vector<Station>& stations,
        std::size_t max_triangles)
    {
        if (stations.size() < 3)
        {
            spdlog::debug("compute_closure_phases: fewer than 3 stations — returning empty");
            return {};
        }
        if (points.empty())
        {
            spdlog::debug("compute_closure_phases: no visibility samples — returning empty");
            return {};
        }

        // ── Step 1: index visibilities by (station_i, station_j, time_index) ──────
        // sample_uv guarantees station_i < station_j; lookups below must canonicalise.
        std::unordered_map<VisKey, const Visibility*, VisKeyHash> vis_map;
        vis_map.reserve(points.size());
        for (const auto& v : points)
        {
            VisKey key { v.station_i, v.station_j, v.time_index };
            // Only keep the first entry for a given key (duplicates should not occur
            // but guard defensively; first-wins is consistent with ascending point order).
            vis_map.emplace(key, &v);
        }

        // ── Step 2: collect the set of time indices present in the data ──────────
        // Used to iterate common-time samples for each triangle.
        std::vector<std::uint32_t> all_times;
        all_times.reserve(points.size());
        for (const auto& v : points)
            all_times.push_back(v.time_index);
        std::sort(all_times.begin(), all_times.end());
        all_times.erase(std::unique(all_times.begin(), all_times.end()), all_times.end());

        // ── Step 3: helper lambda to look up a baseline canonical pair ────────────
        auto lookup = [&](std::uint32_t si, std::uint32_t sj, std::uint32_t tk)
            -> const Visibility*
        {
            // Canonicalise so that si < sj.
            if (si > sj)
                std::swap(si, sj);
            auto it = vis_map.find(VisKey { si, sj, tk });
            return (it != vis_map.end()) ? it->second : nullptr;
        };

        const std::uint32_t n_stations = static_cast<std::uint32_t>(stations.size());

        // ── Step 4: enumerate triangles a < b < c, select first max_triangles
        //            that have at least one fully-covered time sample ───────────────
        std::vector<ClosureTriangle> result;
        result.reserve(max_triangles);

        for (std::uint32_t a = 0; a < n_stations && result.size() < max_triangles; ++a)
        {
            for (std::uint32_t b = a + 1; b < n_stations && result.size() < max_triangles; ++b)
            {
                for (std::uint32_t c = b + 1; c < n_stations && result.size() < max_triangles; ++c)
                {
                    // Collect samples at times where all three baselines are present.
                    ClosureTriangle tri;
                    tri.a = a;
                    tri.b = b;
                    tri.c = c;

                    for (std::uint32_t tk : all_times)
                    {
                        const Visibility* vab = lookup(a, b, tk);
                        const Visibility* vbc = lookup(b, c, tk);
                        const Visibility* vac = lookup(a, c, tk);
                        if (!vab || !vbc || !vac)
                            continue;

                        const double phi_ab = std::atan2(vab->Vi, vab->Vr);
                        const double phi_bc = std::atan2(vbc->Vi, vbc->Vr);
                        const double phi_ac = std::atan2(vac->Vi, vac->Vr);

                        const double tphi_ab = std::atan2(vab->tVi, vab->tVr);
                        const double tphi_bc = std::atan2(vbc->tVi, vbc->tVr);
                        const double tphi_ac = std::atan2(vac->tVi, vac->tVr);

                        tri.samples.push_back(ClosureSample {
                            .time_index = tk,
                            .observed   = wrap_phase(phi_ab + phi_bc - phi_ac),
                            .truth      = wrap_phase(tphi_ab + tphi_bc - tphi_ac),
                        });
                    }

                    if (!tri.samples.empty())
                        result.push_back(std::move(tri));
                }
            }
        }

        if (result.empty())
        {
            spdlog::debug(
                "compute_closure_phases: no triangle found with common-time coverage "
                "({} stations, {} samples)",
                n_stations, points.size());
        }

        return result;
    }

} // namespace parallax::interferometry
