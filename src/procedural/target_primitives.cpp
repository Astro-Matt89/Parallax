/// @file target_primitives.cpp
/// @brief Noise functions and morphological primitives for the procedural target model.
///
/// Port of the "MODELLO TARGET COMPOSITIVO" noise and TargetPrimitives section
/// from tools/glasswing-sandbox-v1_7.4.html.
///
/// ## vhash implementation note
/// The JS oracle uses plain double multiplication for the intermediate step
/// `(h^(h>>>13))*1274126177`.  For inputs where `h^(h>>>13)` has magnitude
/// > sqrt(2^53) this multiplication is not an exact integer — the JS double
/// result differs from modular uint32_t arithmetic.  The `to_js_u32` helper
/// replicates the JS semantics by converting through int64_t (same as
/// ECMAScript `ToInt32` / `ToUint32`).  This is a known structural divergence
/// from the problem-statement's "uint32_t arithmetic" suggestion; the double
/// path matches the oracle output exactly.

// ──────────────────────────────────────────────────────────────────────────────
// Corresponding header
// ──────────────────────────────────────────────────────────────────────────────
#include "procedural/target_families.hpp"

// ──────────────────────────────────────────────────────────────────────────────
// Standard library
// ──────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace parallax::procedural
{

// ═══════════════════════════════════════════════════════════════════════════════
// Internal helpers
// ═══════════════════════════════════════════════════════════════════════════════

/// Replicate JS `(d >>> 0)`: convert double → Uint32 via two's-complement
/// truncation.  Equivalent to ECMAScript ToUint32.
static inline std::uint32_t to_js_u32(double d) noexcept
{
    // JS: ToUint32(d) = ModuloUint32(ToInteger(d))
    //                 = (trunc(d) mod 2^32 + 2^32) mod 2^32
    // C++ int64_t cast truncates (same as trunc for in-range values), then
    // uint64_t reinterpretation gives two's-complement; low 32 bits = Uint32.
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(static_cast<std::int64_t>(d)) & 0xFFFFFFFFULL);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Noise functions
// ═══════════════════════════════════════════════════════════════════════════════

// JS:
//   function vhash(ix,iy,s){
//     let h=ix*374761393+iy*668265263+(s|0)*974634;
//     h=(h^(h>>>13))*1274126177;
//     return ((h^(h>>>16))>>>0)/4294967296;}
double vhash(std::int32_t ix, std::int32_t iy, std::int32_t s) noexcept
{
    // Draw 0: initial sum in JS double arithmetic (no bitwise ops yet).
    double h_d = static_cast<double>(ix) * 374761393.0
               + static_cast<double>(iy) * 668265263.0
               + static_cast<double>(s)  * 974634.0;

    // Convert to uint32 (low 32 bits, matching JS >>> operator).
    const std::uint32_t h = to_js_u32(h_d);

    // `h^(h>>>13)` — ^ in JS converts to Int32 first.
    const std::uint32_t xv = h ^ (h >> 13u);
    const std::int32_t  xv_i = static_cast<std::int32_t>(xv);

    // Multiply as JS double (potentially inexact for large values, matching oracle).
    const double prod = static_cast<double>(xv_i) * 1274126177.0;

    // ((h^(h>>>16))>>>0): get Uint32 of prod, then XOR shift.
    const std::uint32_t h2 = to_js_u32(prod);

    return static_cast<double>(h2 ^ (h2 >> 16u)) / 4294967296.0;
}

// JS:
//   function vnoise(x,y,s){
//     const ix=Math.floor(x),iy=Math.floor(y),fx=x-ix,fy=y-iy;
//     const sm=t=>t*t*(3-2*t);
//     const a=vhash(ix,iy,s),b=vhash(ix+1,iy,s),
//           c=vhash(ix,iy+1,s),d=vhash(ix+1,iy+1,s);
//     return a+(b-a)*sm(fx)+(c-a)*sm(fy)+(a-b-c+d)*sm(fx)*sm(fy);}
double vnoise(double x, double y, std::int32_t s) noexcept
{
    const auto ix = static_cast<std::int32_t>(std::floor(x));
    const auto iy = static_cast<std::int32_t>(std::floor(y));
    const double fx = x - static_cast<double>(ix);
    const double fy = y - static_cast<double>(iy);

    // Smoothstep: t² (3 - 2t)
    auto sm = [](double t) noexcept { return t * t * (3.0 - 2.0 * t); };

    const double a = vhash(ix,     iy,     s);
    const double b = vhash(ix + 1, iy,     s);
    const double c = vhash(ix,     iy + 1, s);
    const double d = vhash(ix + 1, iy + 1, s);

    const double sfx = sm(fx);
    const double sfy = sm(fy);
    return a + (b - a) * sfx + (c - a) * sfy + (a - b - c + d) * sfx * sfy;
}

// JS:
//   function fbm2(x,y,s){
//     return vnoise(x,y,s)*0.55
//          + vnoise(x*2.1,y*2.1,s+11)*0.30
//          + vnoise(x*4.3,y*4.3,s+23)*0.15;}
double fbm2(double x, double y, std::int32_t s) noexcept
{
    return vnoise(x,       y,       s)      * 0.55
         + vnoise(x * 2.1, y * 2.1, s + 11) * 0.30
         + vnoise(x * 4.3, y * 4.3, s + 23) * 0.15;
}

// ═══════════════════════════════════════════════════════════════════════════════
// TargetPrimitives — internal rendering helpers
// (not exposed in the header; used only by render_target_at via target_families.cpp)
// ═══════════════════════════════════════════════════════════════════════════════

// JS: _splat(sky,cx,cy,sig,amp)
void prim_splat(std::vector<double>& sky,
                double cx, double cy, double sig, double amp,
                std::uint32_t N) noexcept
{
    const int r  = static_cast<int>(std::ceil(sig * 4.0));
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - r)));
    const int x1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(cx + r)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - r)));
    const int y1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(cy + r)));

    const double inv2sig2 = 1.0 / (2.0 * sig * sig);
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            sky[static_cast<std::uint32_t>(y) * N + static_cast<std::uint32_t>(x)]
                += amp * std::exp(-(dx * dx + dy * dy) * inv2sig2);
        }
    }
}

// JS: point(sky,c)
void prim_point(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    prim_splat(sky, c.x, c.y, c.sigma > 0.0 ? c.sigma : 1.3, c.flux, N);
}

// JS: gaussian(sky,c) — elliptic rotated
void prim_gaussian(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    const double sx = c.sigma_x;
    const double sy = (c.sigma_y > 0.0) ? c.sigma_y : c.sigma_x;
    const double co = std::cos(c.angle);
    const double si = std::sin(c.angle);
    const int    rv = static_cast<int>(std::ceil(std::max(sx, sy) * 3.5));
    const int x0 = std::max(0, static_cast<int>(std::floor(c.x - rv)));
    const int x1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.x + rv)));
    const int y0 = std::max(0, static_cast<int>(std::floor(c.y - rv)));
    const int y1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.y + rv)));

    const double inv2sx2 = 1.0 / (2.0 * sx * sx);
    const double inv2sy2 = 1.0 / (2.0 * sy * sy);
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = x - c.x;
            const double dy = y - c.y;
            const double u  =  dx * co + dy * si;
            const double v  = -dx * si + dy * co;
            sky[static_cast<std::uint32_t>(y) * N + static_cast<std::uint32_t>(x)]
                += c.flux * std::exp(-(u * u * inv2sx2 + v * v * inv2sy2));
        }
    }
}

// JS: disk(sky,c) — limb darkening + ellipticity
void prim_disk(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    const double R  = c.radius;
    const double el = c.ellipticity;
    const double ld = c.limb_darkening;
    const double co = std::cos(c.angle);
    const double si = std::sin(c.angle);
    const double Ry = R * (1.0 - el);
    const int    rv = static_cast<int>(std::ceil(R + 1.0));
    const int x0 = std::max(0, static_cast<int>(std::floor(c.x - rv)));
    const int x1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.x + rv)));
    const int y0 = std::max(0, static_cast<int>(std::floor(c.y - rv)));
    const int y1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.y + rv)));

    const double invR2  = 1.0 / (R * R);
    const double invRy2 = 1.0 / (Ry * Ry);
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = x - c.x;
            const double dy = y - c.y;
            const double u  =  dx * co + dy * si;
            const double v  = -dx * si + dy * co;
            const double m  = u * u * invR2 + v * v * invRy2;
            if (m < 1.0)
            {
                const double mu = std::sqrt(1.0 - m);
                sky[static_cast<std::uint32_t>(y) * N + static_cast<std::uint32_t>(x)]
                    += c.flux * (1.0 - ld * (1.0 - mu));
            }
        }
    }
}

// JS: ring(sky,c) — Gaussian radial profile, ellipticity, azimuthal harmonics
void prim_ring(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    const double R  = c.radius;
    const double w  = c.width;
    const double el = c.ellipticity;
    const double co = std::cos(c.angle);
    const double si = std::sin(c.angle);
    const int    rv = static_cast<int>(std::ceil(R + w * 3.5));
    const int x0 = std::max(0, static_cast<int>(std::floor(c.x - rv)));
    const int x1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.x + rv)));
    const int y0 = std::max(0, static_cast<int>(std::floor(c.y - rv)));
    const int y1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.y + rv)));

    const double inv2w2  = 1.0 / (2.0 * w * w);
    const double inv1me  = 1.0 / (1.0 - el);
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = x - c.x;
            const double dy = y - c.y;
            const double xr =  dx * co + dy * si;
            const double yr = (-dx * si + dy * co) * inv1me;
            const double rd = std::hypot(xr, yr);
            const double ph = std::atan2(yr, xr);

            double az = 1.0;
            for (const auto& h : c.harm)
                az += h.A * std::cos(h.k * ph - h.ph);

            const double dr = rd - R;
            const double v  = c.flux * std::exp(-dr * dr * inv2w2) * std::max(0.05, az);
            if (v > 1e-4)
                sky[static_cast<std::uint32_t>(y) * N + static_cast<std::uint32_t>(x)] += v;
        }
    }
}

// JS: jet(sky,c) — baked knot sequence, curvature, counter-jet
void prim_jet(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    for (const auto& k : c.knots)
    {
        const double ang = c.angle + c.curvature * k.t * 2.0 + k.dAng;
        const double ca  = std::cos(ang);
        const double sa  = std::sin(ang);
        prim_splat(sky,
                   c.x + ca * c.length * k.t,
                   c.y + sa * c.length * k.t,
                   k.sig,
                   c.flux * k.amp,
                   N);
        if (c.counter_jet_ratio > 0.0)
        {
            prim_splat(sky,
                       c.x - ca * c.length * k.t * 0.8,
                       c.y - sa * c.length * k.t * 0.8,
                       k.sig,
                       c.flux * k.amp * c.counter_jet_ratio,
                       N);
        }
    }
}

// JS: planet_surface(sky,c)
// λ-dependent effects use the module-level render_nu (threaded as parameter).
void prim_planet_surface(std::vector<double>& sky, const Component& c,
                          std::uint32_t N, double render_nu) noexcept
{
    const double R   = c.radius;
    const double lam = kCLight / render_nu;

    // Cloud band strength (strong in VIS, weak in radio)
    const double cloud_band = std::max(0.35, std::min(1.0, (2e-6 - lam) / 1.5e-6));

    // Chromatic flag: only for optical/NIR
    const bool chrom = lam < 2.5e-6;
    const double tC  = chrom
        ? std::max(0.0, std::min(1.0, (lam * 1e9 - 400.0) / 300.0))
        : 0.5;
    const bool nir = chrom && (lam > 7.5e-7);

    // Illumination direction
    const double Lx = std::sin(c.phase_angle);
    const double Lz = std::cos(c.phase_angle);
    const double Ly = 0.15;
    const double Ln = std::hypot(Lx, std::hypot(Ly, Lz));

    const bool is_giant = (c.kind == "planet_giant");
    const bool is_arid  = (c.kind == "planet_arid");
    const bool is_ice   = (c.kind == "planet_ice");

    const int x0 = std::max(0, static_cast<int>(std::floor(c.x - R - 1.0)));
    const int x1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.x + R + 1.0)));
    const int y0 = std::max(0, static_cast<int>(std::floor(c.y - R - 1.0)));
    const int y1 = std::min(static_cast<int>(N) - 1, static_cast<int>(std::ceil(c.y + R + 1.0)));

    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const double dx = (x - c.x) / R;
            const double dy = (y - c.y) / R;
            const double r2 = dx * dx + dy * dy;
            if (r2 >= 1.0)
                continue;

            const double nz  = std::sqrt(1.0 - r2);
            const double lat = std::asin(dy);
            const double lon = std::atan2(dx, nz) + c.rot_phase;

            // Periodic UV coordinates (longitude-wrapped)
            const double u1 = std::cos(lon) * 1.4;
            const double u2 = std::sin(lon) * 1.4;

            double alb = 0.5;
            if (is_giant)
            {
                const double dist = fbm2(u1 * 0.8 + lat * 0.4, u2 * 0.8 + lat * 2.6, c.nz) - 0.5;
                const double band = std::sin(lat * 7.5 + dist * 3.2);
                alb = 0.55 + 0.30 * band;
                if (chrom && band < 0.0)
                    alb *= 0.55 + 0.75 * tC;
            }
            else
            {
                const double h = fbm2(u1 + lat * 0.35, u2 + lat * 1.7, c.nz);
                if (is_arid)
                {
                    alb = 0.42 + 0.38 * h;
                    if (chrom)
                        alb *= nir ? 1.30 : (0.55 + 0.85 * tC);
                }
                else if (h > c.sea_level)
                {
                    const double rel = (h - c.sea_level) / (1.0 - c.sea_level);
                    alb = 0.5 + 0.35 * rel;
                    if (chrom)
                    {
                        if (rel < 0.35)
                            alb *= nir
                                ? 1.60
                                : (0.55 + 0.55 * std::exp(-(tC - 0.45) * (tC - 0.45) / 0.06));
                        else
                            alb *= 0.62 + 0.70 * tC;
                    }
                }
                else
                {
                    alb = (is_ice ? 0.32 : 0.16) + 0.06 * h;
                    if (chrom)
                        alb *= nir ? 0.12 : (1.55 - 1.15 * tC);
                }
                if (!is_arid && std::abs(lat) > c.cap_lat)
                    alb = nir ? 0.75 : 0.88;
                if (is_ice)
                    alb = 0.45 + alb * 0.55;
            }

            // Cloud layer
            if (c.cloud_amt > 0.0 && !is_giant)
            {
                const double dl    = c.cloud_phase - c.rot_phase;
                const double cl    = fbm2(std::cos(lon + dl) * 2.2 + lat * 0.5,
                                          std::sin(lon + dl) * 2.2 + lat * 2.4,
                                          c.nz + 77);
                const double cover = std::max(0.0, (cl - 0.52) * 3.0) * c.cloud_amt * cloud_band;
                alb = alb * (1.0 - std::min(1.0, cover)) + 0.95 * std::min(1.0, cover);
            }

            const double illum = std::max(0.03, (dx * Lx + dy * Ly + nz * Lz) / Ln);
            sky[static_cast<std::uint32_t>(y) * N + static_cast<std::uint32_t>(x)]
                += c.flux * alb * illum;
        }
    }
}

// JS: absorption(sky,c) — multiplicative attenuation applied to rendered flux
void prim_absorption(std::vector<double>& sky, const Component& c, std::uint32_t N) noexcept
{
    const std::uint32_t total = N * N;
    for (std::uint32_t idx = 0; idx < total; ++idx)
    {
        const std::uint32_t xi = idx % N;
        const std::uint32_t yi = idx / N;
        const double dx = static_cast<double>(xi) - c.x;
        const double dy = static_cast<double>(yi) - c.y;
        double f = 1.0;

        if (c.shape == "disk")
        {
            const double m = (dx * dx + dy * dy) / (c.radius * c.radius);
            if (m < 1.0)
                f = 1.0 - c.depth * std::min(1.0, (1.0 - m) * 3.0);
        }
        else if (c.shape == "gaussian")
        {
            f = 1.0 - c.depth * std::exp(-(dx * dx + dy * dy) / (2.0 * c.sigma * c.sigma));
        }
        else if (c.shape == "band")
        {
            const double d = std::abs(-dx * std::sin(c.angle) + dy * std::cos(c.angle));
            f = 1.0 - c.depth * std::exp(-d * d / (2.0 * c.width * c.width));
        }

        if (f < 1.0)
            sky[idx] *= std::max(0.0, f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Spectral-model evaluation
// ─────────────────────────────────────────────────────────────────────────────

struct SpectralEval { double flux_scale; double size_scale; };

static SpectralEval eval_spectral(const Component& c, double nu) noexcept
{
    const double ref = (c.reference_freq_hz > 0.0) ? c.reference_freq_hz : 230e9;
    const double r   = nu / ref;
    SpectralEval e{1.0, 1.0};

    auto clamp = [](double v, double lo, double hi) noexcept {
        return v < lo ? lo : (v > hi ? hi : v);
    };

    switch (c.spectral_model)
    {
    case SpectralModel::Stellar:
        e.flux_scale = std::pow(r, 2.0);
        e.size_scale = 1.0;
        break;
    case SpectralModel::ThermalDust:
        e.flux_scale = std::pow(r, 3.5);
        e.size_scale = clamp(std::pow(r, 0.15), 0.75, 1.3);
        break;
    case SpectralModel::Synchrotron:
        e.flux_scale = std::pow(r, c.alpha);
        e.size_scale = clamp(std::pow(r, c.size_index), 0.6, 2.2);
        break;
    case SpectralModel::FreeFree:
        e.flux_scale = std::pow(r, -0.1);
        e.size_scale = 1.0;
        break;
    case SpectralModel::Maser:
    {
        const double line = (c.line_freq_hz > 0.0) ? c.line_freq_hz : 22.235e9;
        const double d    = (std::log10(nu) - std::log10(line)) / 0.09;
        e.flux_scale = c.maser_amp * std::exp(-d * d);
        e.size_scale = 1.0;
        break;
    }
    }

    if (!std::isfinite(e.flux_scale) || e.flux_scale < 0.0) e.flux_scale = 0.0;
    e.flux_scale = std::min(e.flux_scale, 1e5);
    if (!std::isfinite(e.size_scale) || e.size_scale <= 0.0) e.size_scale = 1.0;
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// scaleComp: build a rendering copy of a component with scaled geometry/flux
// ─────────────────────────────────────────────────────────────────────────────
static Component scale_comp(const Component& src, double size_scale, double eff_flux) noexcept
{
    Component d = src;
    d.flux = eff_flux;
    if (size_scale != 1.0)
    {
        d.sigma   *= size_scale;
        d.sigma_x *= size_scale;
        d.sigma_y *= size_scale;
        d.radius  *= size_scale;
        d.width   *= size_scale;
        if (d.type == PrimitiveType::Jet)
            d.length *= size_scale;
    }
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch: call the right primitive with a component
// ─────────────────────────────────────────────────────────────────────────────
void dispatch_primitive(std::vector<double>& sky, const Component& c,
                         std::uint32_t N, double render_nu) noexcept
{
    switch (c.type)
    {
    case PrimitiveType::Point:        prim_point(sky, c, N);        break;
    case PrimitiveType::Gaussian:     prim_gaussian(sky, c, N);     break;
    case PrimitiveType::Disk:         prim_disk(sky, c, N);         break;
    case PrimitiveType::Ring:         prim_ring(sky, c, N);         break;
    case PrimitiveType::Jet:          prim_jet(sky, c, N);          break;
    case PrimitiveType::PlanetSurface:prim_planet_surface(sky, c, N, render_nu); break;
    case PrimitiveType::Absorption:   /* handled separately */       break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// renderSky — main rendering entry point (called by target_families.cpp)
// Implements renderTargetAt() logic (without the optical / nulled branches,
// which are not part of the 10b.6 contract).
// ─────────────────────────────────────────────────────────────────────────────
std::vector<double> render_sky(const std::vector<Component>& source_components,
                                double nu, std::uint32_t N)
{
    std::vector<double> sky(static_cast<std::size_t>(N) * N, 0.0);

    // Separate emission from absorption
    std::vector<const Component*> em_comps;
    em_comps.reserve(source_components.size());
    std::vector<const Component*> abs_comps;

    for (const auto& c : source_components)
    {
        if (c.type == PrimitiveType::Absorption)
            abs_comps.push_back(&c);
        else
            em_comps.push_back(&c);
    }

    // Evaluate spectral scaling for emission components
    struct EvalEntry { const Component* comp; double eff; double size; };
    std::vector<EvalEntry> evals;
    evals.reserve(em_comps.size());

    double mx = 0.0;
    for (const Component* cp : em_comps)
    {
        const double flux_ref = (cp->flux_ref != 0.0) ? cp->flux_ref : cp->flux;
        const auto   sp       = eval_spectral(*cp, nu);
        const double eff      = flux_ref * sp.flux_scale;
        if (eff > mx) mx = eff;
        evals.push_back({cp, eff, sp.size_scale});
    }

    const double norm = (mx > 0.0) ? mx : 1.0;

    for (const auto& ev : evals)
    {
        if (mx > 0.0 && ev.eff < 2e-4 * mx)
            continue;  // below 1:5000 visibility threshold

        const Component sc = scale_comp(*ev.comp, ev.size, ev.eff / norm);
        dispatch_primitive(sky, sc, N, nu);
    }

    // Apply absorption passes
    for (const Component* ap : abs_comps)
        prim_absorption(sky, *ap, N);

    return sky;
}

} // namespace parallax::procedural
