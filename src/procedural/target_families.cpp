/// @file target_families.cpp
/// @brief Public API: generate_target_model, render_target_at,
///        compute_target_fft, target_from_object_seed.
///
/// Ported from tools/glasswing-sandbox-v1_7.4.html:
///   generateTargetModel, renderTargetAt, computeTargetFFT, applyTemporal.
///
/// ## Structural divergences from the sandbox (numbers unchanged)
/// - Global `N` and `RENDER_NU` are threaded as explicit parameters.
/// - `lambda_m` and `epoch_days` are passed in; applyTemporal and
///   evaluateSpectralFlux are both called (the sandbox v0.3 comment "not yet
///   used" is outdated — v1.7.4 actually calls both).
/// - `nulled` / `fd` (coronagraph / optical path) are NOT part of the 10b.6
///   contract and are omitted from the C++ API.
///
/// ## Sprint 07 integration seam
/// `target_from_object_seed` maps a 64-bit universe object ID to a TargetModel
/// by using the lower 32 bits as the generation seed.  Full wiring to
/// ProceduralProvider / KnowledgeDatabase is deferred — the function provides a
/// clean, tested entry point for that integration.  See CLAUDE.md §7c.

// ──────────────────────────────────────────────────────────────────────────────
// Project headers
// ──────────────────────────────────────────────────────────────────────────────
#include "target_internal.hpp"
#include "core/fft.hpp"

// ──────────────────────────────────────────────────────────────────────────────
// Standard library
// ──────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace parallax::procedural
{

// ─────────────────────────────────────────────────────────────────────────────
// applyTemporal (internal)
// Returns a modified copy of model.components with temporal state applied.
// Matches the JS `applyTemporal(model, tDays)` exactly.
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<Component> apply_temporal(const TargetModel& model,
                                              double epoch_days,
                                              std::uint32_t N)
{
    const TemporalData& T = model.temporal;
    const double tY = epoch_days / 365.25;
    if (T.model == TemporalModel::Static || tY == 0.0)
        return model.components;

    const double C = static_cast<double>(N) / 2.0;
    const double k2Pi = 2.0 * std::numbers::pi;

    // Helper: rotate component c by angle ang around C
    auto rot = [&](const Component& c, double ang) -> Component
    {
        Component d = c;
        const double dx = c.x - C;
        const double dy = c.y - C;
        const double coAng = std::cos(ang);
        const double siAng = std::sin(ang);
        d.x = C + dx * coAng - dy * siAng;
        d.y = C + dx * siAng + dy * coAng;
        if (d.angle != 0.0 || c.type == PrimitiveType::Gaussian
                             || c.type == PrimitiveType::Ring
                             || c.type == PrimitiveType::Jet)
        {
            d.angle = c.angle + ang;
        }
        return d;
    };

    std::vector<Component> out;
    out.reserve(model.components.size());

    switch (T.model)
    {
    case TemporalModel::PlanetRotation:
    {
        // The planet rotates; clouds drift slightly faster (×1.18)
        const double dphi = k2Pi * tY / (T.period_years > 0.0 ? T.period_years : 1.0);
        for (const auto& c : model.components)
        {
            if (c.type != PrimitiveType::PlanetSurface)
            {
                out.push_back(c);
            }
            else
            {
                Component d = c;
                d.rot_phase   = c.rot_phase   + dphi;
                d.cloud_phase = c.cloud_phase + dphi * 1.18;
                out.push_back(d);
            }
        }
        break;
    }
    case TemporalModel::MultiOrbit:
    {
        // Each component with an orbit uses its own Keplerian period
        for (const auto& c : model.components)
        {
            if (!c.orbit)
            {
                out.push_back(c);
            }
            else
            {
                const Orbit& o = *c.orbit;
                const double ang = o.phase0 + k2Pi * tY / o.periodYears;
                const double lx  = o.aPx * std::cos(ang);
                const double ly  = o.aPx * std::sin(ang) * o.cosI;
                Component d = c;
                d.x = C + lx * std::cos(o.pa) - ly * std::sin(o.pa);
                d.y = C + lx * std::sin(o.pa) + ly * std::cos(o.pa);
                out.push_back(d);
            }
        }
        break;
    }
    case TemporalModel::Orbit:
    {
        // Circular orbital rotation of the whole system.
        // JS: ang = (T.phase0||0)*0 + 2π*tY/(T.periodYears||1)
        // Note: the *0 means phase0 is discarded — rotation starts from 0.
        const double ang = k2Pi * tY / (T.period_years > 0.0 ? T.period_years : 1.0);
        for (const auto& c : model.components)
        {
            if (c.id == "compagna" || c.type == PrimitiveType::Absorption)
                out.push_back(c);
            else
                out.push_back(rot(c, ang));
        }
        break;
    }
    case TemporalModel::Rotation:
    {
        // Co-rotating spots/bands
        const double ang = k2Pi * tY / (T.period_years > 0.0 ? T.period_years : 1.0);
        for (const auto& c : model.components)
        {
            if (c.rot || c.id == "hotspot")
                out.push_back(rot(c, ang));
            else
                out.push_back(c);
        }
        break;
    }
    case TemporalModel::Expansion:
    {
        // Shell expands: R(t) = R0 * (1 + t/age)
        const double ageY = model.physical.age_years > 0.0
            ? model.physical.age_years : 1000.0;
        const double f = 1.0 + tY / ageY;
        for (const auto& c : model.components)
        {
            if (c.type == PrimitiveType::Ring)
            {
                Component d = c;
                d.radius = c.radius * f;
                d.width  = c.width  * std::sqrt(f);
                out.push_back(d);
            }
            else if (c.id.rfind("maser_", 0) == 0)
            {
                Component d = c;
                d.x = C + (c.x - C) * f;
                d.y = C + (c.y - C) * f;
                out.push_back(d);
            }
            else
            {
                out.push_back(c);
            }
        }
        break;
    }
    case TemporalModel::ProperMotion:
    {
        // Jet knots advance; amplitudes and sizes evolve
        const double Lpc = model.physical.jet_length_pc > 0.0
            ? model.physical.jet_length_pc : 10.0;
        const double dt = tY * T.knot_speed_c / (Lpc * 3.261);
        for (const auto& c : model.components)
        {
            if (c.type != PrimitiveType::Jet)
            {
                out.push_back(c);
            }
            else
            {
                Component d = c;
                d.knots.clear();
                d.knots.reserve(c.knots.size());
                for (const auto& k : c.knots)
                {
                    const double t2 = std::fmod(k.t + dt, 1.0);
                    JetKnot kn;
                    kn.t    = t2;
                    kn.dAng = k.dAng;
                    kn.amp  = std::pow(1.0 - t2, 1.6) * 0.7 + 0.03;
                    kn.sig  = 1.2 + t2 * 3.2;
                    d.knots.push_back(kn);
                }
                out.push_back(d);
            }
        }
        break;
    }
    default:
        return model.components;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// generate_target_model (public)
// ─────────────────────────────────────────────────────────────────────────────

TargetModel generate_target_model(std::uint32_t seed, const TargetOptions& opts)
{
    // N is 128 (fixture contract default) for generation; rendering uses
    // the N passed to render_target_at.  Generation only needs N for the
    // pixel-layout of components, so we use the default render N=128.
    constexpr std::uint32_t kGenN = 128u;

    Rng rng{seed};

    // Step 1: family selection (draw only if not forced)
    Family fam;
    if (opts.forced_family.has_value())
    {
        fam = *opts.forced_family;
    }
    else
    {
        const double fd = rng.next();  // draw: family selection
        fam = static_cast<Family>(static_cast<std::uint8_t>(fd * kFamilyCount));
    }

    TargetModel m;
    m.seed        = seed;
    m.designation = generate_designation(seed);
    m.family      = fam;
    m.fam_idx     = static_cast<int>(fam);
    m.rarity      = Rarity::Common;
    m.fov_mul     = 2.5;
    m.theta_obj   = 0.0;
    m.temporal.model = TemporalModel::Static;

    // Step 2: recipe body
    dispatch_recipe(m, rng, kGenN, fam);

    // Step 3–7: rarity + budget + capRoll + shuffle + modifiers
    apply_compatible_modifiers(m, rng, opts.complexity, kGenN);

    // Truncate to MAX_COMPONENTS
    if (m.components.size() > kMaxComponents)
        m.components.resize(kMaxComponents);

    // Summary label
    m.summary = family_label(fam) + " \u00b7 " +
                (m.subtype.empty() ? m.subtype : subtype_label(m.subtype));

    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// render_target_at (public)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<double> render_target_at(const TargetModel& model,
                                      double        lambda_m,
                                      double        epoch_days,
                                      std::uint32_t N)
{
    // Apply temporal evolution to get the component set for this epoch
    const auto source = apply_temporal(model, epoch_days, N);

    // Delegate to render_sky (target_primitives.cpp) which handles spectral
    // evaluation, the 1:5000 cutoff, and the two-pass emission/absorption loop
    return render_sky(source, lambda_m, N);
}

// ─────────────────────────────────────────────────────────────────────────────
// compute_target_fft (public)
// ─────────────────────────────────────────────────────────────────────────────
// Matches JS computeTargetFFT exactly:
//   flux = sum(sky)
//   re = copy(sky); im = zeros
//   shift2(re); fft2(re, im, false); shift2(re); shift2(im)
//   return {re, im, flux}

parallax::interferometry::TargetFT compute_target_fft(
    const std::vector<double>& sky, std::uint32_t N)
{
    const std::size_t sz = static_cast<std::size_t>(N) * N;
    assert(sky.size() == sz);

    // Total flux (DC)
    double flux = 0.0;
    for (double v : sky) flux += v;

    // Forward 2-D FFT with the same shift convention as the sandbox
    std::vector<double> re(sky.begin(), sky.end());
    std::vector<double> im(sz, 0.0);
    parallax::core::shift2(re, N);
    parallax::core::fft2(re, im, N);  // forward transform (matches fft2(re,im,false))
    parallax::core::shift2(re, N);
    parallax::core::shift2(im, N);

    parallax::interferometry::TargetFT ft;
    ft.Fre        = std::move(re);
    ft.Fim        = std::move(im);
    ft.N          = N;
    ft.flux_total = flux;
    return ft;
}

// ─────────────────────────────────────────────────────────────────────────────
// target_from_object_seed (public)
// ─────────────────────────────────────────────────────────────────────────────
// Sprint 07 seam: deterministic generation from a 64-bit universe object ID.
// Uses the lower 32 bits as the model seed, matching the ProceduralProvider
// convention that object IDs are (type << 56) | source_id with source_id
// fitting in 32 bits.
//
// Full wiring to ProceduralProvider::query_fov → Universe → KnowledgeDatabase
// is deferred (tracked issue: follow-up PR after 10b.7 fixtures are validated).
// This function provides the tested, stable entry point for that integration.
//
// Knowledge-level mapping intent (for the follow-up PR):
//   L1: position, magnitude → generate_target_model gives theta_obj, family
//   L2: spectral type, morphology class → m.family, m.spectral_model of components
//   L3: size, basic structure → m.theta_obj, m.fov_mul, m.subtype
//   L4+: detailed physics → m.physical.*, m.components
//
TargetModel target_from_object_seed(std::uint64_t object_seed,
                                     const TargetOptions& opts)
{
    const std::uint32_t seed32 = static_cast<std::uint32_t>(object_seed & 0xFFFFFFFFu);
    return generate_target_model(seed32, opts);
}

} // namespace parallax::procedural
