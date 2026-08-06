/// @file target_recipes.cpp
/// @brief Procedural target recipes: 8 families + 8 modifiers.
///
/// Ported from tools/glasswing-sandbox-v1_7.4.html, section
/// "MODELLO TARGET COMPOSITIVO" → TargetRecipes + TargetModifiers +
/// applyCompatibleModifiers + generateDesignation.
///
/// ## RNG draw-order per recipe (BINDING CONTRACT — see SPECIFICA_10b §2)
///
/// BINARY:
///   1 draw: PA
///   1 draw: branch (< 0.72 → wide_binary, else contact_binary)
///   wide_binary:  aAU · dPc · q · Mtot · phase0
///   contact_binary: R1s · q · dPc · phase0 · period
///
/// STAR:
///   1 draw: PA
///   1 draw: roll (selects subtype)
///   supergiant:      Rsun · dPc · ld · phase0 · period [· env()]
///   oblate_star:     Rsun · dPc · ld · e · phase0 · period [· env()]
///   brown_dwarf:     Rj · dPc · T · nb_frac · nb·band_off · storm_roll
///                    [· storm_a · storm_rd] · rotH · phase0
///   t_tauri:         RdAU · dPc · inc · 3·knot_dAng · knot_speed
///   wolf_rayet:      RspAU · dKpc · P
///   dying_supergiant: Rsun · dPc · shellF · nc_frac · nc·(a+rd) ·
///                      2·shells·(ell+ph) · maser_roll [· line · 3·(a+amp)] ·
///                      shell_age
///
/// PROTO_DISK:
///   1 draw: PA
///   rr: RAU · dPc · inc · roll
///   classic_disk:    gap [no extra draws for star]
///   transition_disk: r0_frac
///   multi_ring_disk: nr_frac
///   methanol_maser branch (roll < 0.3): nm_frac · nm·(a+rd+amp)
///
/// NOVA:
///   1 draw: PA
///   1 draw: branch (< 0.25 → pulsar, else nova_shell)
///   pulsar:     Rpc · dKpc · ell_neb · ph_neb · periodMs · dmPcCm3
///   nova_shell: RAU · dKpc · ell · k1_frac · ph1 · k2_frac · ph2 · vExp ·
///               maser_roll [· ns_frac · line_frac · ns·(a+amp)]
///
/// AGN:
///   1 draw: PA
///   1 draw: branch (< 0.62 → core_jet, else double_lobe)
///   core_jet:   Lpc · dMpc · nb_frac · nb·knot_dAng · aCore · aJet · knotSpeed
///   double_lobe: Lkpc · dMpc · lr_frac · asym · aLobe · aHS · aCore(hotspot)
///
/// COMPACT:
///   1 draw: PA
///   thUas · dMpc · A · alpha_ring
///
/// PLANETARY:
///   1 draw: PA
///   dPc · Mstar · aOut
///   sculpted_disk: inc · rIn_frac · np_frac · np·(aA_frac+cst+phase0)
///   young_system:  cosI_inc · np_frac · ratio · np·(cst+phase0)
///
/// PLANET_RES:
///   ki_frac · Re(giant/else) · dPc · rotH(giant/else) ·
///   nz · rotPhase · cloudPhase · phaseAngle · seaLevel · capLat ·
///   cloudAmt[giant:0 else:1 draw] · [no more draws]
///
/// applyCompatibleModifiers:
///   rarity_roll · budget_draw(1 always) · capRoll ·
///   Fisher-Yates: 1 draw per swap (i from len-1 down to 1) ·
///   modifier internal draws

// ──────────────────────────────────────────────────────────────────────────────
// Project headers
// ──────────────────────────────────────────────────────────────────────────────
#include "target_internal.hpp"

// ──────────────────────────────────────────────────────────────────────────────
// Standard library
// ──────────────────────────────────────────────────────────────────────────────
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace parallax::procedural
{

static constexpr double k2Pi = 2.0 * std::numbers::pi;

// Helper: rr(rng, a, b) = a + rng.next() * (b − a)
// Each call consumes exactly one draw.  Never embed multiple calls of this
// in the same expression — bind each result to a named local first.
static inline double rr(Rng& rng, double a, double b) noexcept
{
    return a + rng.next() * (b - a);
}

// ─────────────────────────────────────────────────────────────────────────────
// Component builder helpers
// ─────────────────────────────────────────────────────────────────────────────

static Component make_point(std::string id, double x, double y,
                             double sigma, double flux,
                             SpectralModel sm, double ref_hz,
                             double alpha = -0.7, double size_idx = 0.0)
{
    Component c;
    c.id = std::move(id);
    c.type = PrimitiveType::Point;
    c.x = x; c.y = y;
    c.sigma = sigma;
    c.flux = flux; c.flux_ref = flux;
    c.spectral_model = sm;
    c.reference_freq_hz = ref_hz;
    c.alpha = alpha; c.size_index = size_idx;
    return c;
}

static Component make_gaussian(std::string id, double x, double y,
                                double sx, double sy, double angle,
                                double flux, SpectralModel sm, double ref_hz,
                                double alpha = -0.7)
{
    Component c;
    c.id = std::move(id);
    c.type = PrimitiveType::Gaussian;
    c.x = x; c.y = y;
    c.sigma_x = sx; c.sigma_y = sy;
    c.angle = angle;
    c.flux = flux; c.flux_ref = flux;
    c.spectral_model = sm;
    c.reference_freq_hz = ref_hz;
    c.alpha = alpha;
    return c;
}

static Component make_disk(std::string id, double x, double y,
                            double radius, double ell, double angle,
                            double ld, double flux,
                            SpectralModel sm, double ref_hz)
{
    Component c;
    c.id = std::move(id);
    c.type = PrimitiveType::Disk;
    c.x = x; c.y = y;
    c.radius = radius; c.ellipticity = ell;
    c.angle = angle; c.limb_darkening = ld;
    c.flux = flux; c.flux_ref = flux;
    c.spectral_model = sm;
    c.reference_freq_hz = ref_hz;
    return c;
}

static Component make_ring(std::string id, double x, double y,
                            double radius, double width,
                            double ell, double angle,
                            std::vector<HarmonicTerm> harm,
                            double flux, SpectralModel sm, double ref_hz,
                            double alpha = -0.7)
{
    Component c;
    c.id = std::move(id);
    c.type = PrimitiveType::Ring;
    c.x = x; c.y = y;
    c.radius = radius; c.width = width;
    c.ellipticity = ell; c.angle = angle;
    c.harm = std::move(harm);
    c.flux = flux; c.flux_ref = flux;
    c.spectral_model = sm;
    c.reference_freq_hz = ref_hz;
    c.alpha = alpha;
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// BINARY recipe
// Draw order: PA · branch · [wide: aAU·dPc·q·Mtot·phase0]
//                           [contact: R1s·q·dPc·phase0·period]
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_binary(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;                  // draw 1

    if (rng.next() < 0.72)                                 // draw 2 — branch
    {
        // ── wide binary (legacy) ─────────────────────────────────────────────
        m.subtype  = "wide_binary";
        m.fov_mul  = 2.8;

        const double aAU  = rr(rng, 0.5, 30.5);           // draw 3
        const double dPc  = rr(rng, 5.0, 305.0);          // draw 4
        const double q    = rr(rng, 0.15, 0.95);          // draw 5
        m.theta_obj = (aAU / dPc) / 206264.806;

        const double sep  = static_cast<double>(N) / m.fov_mul;
        const double coPA = std::cos(PA);
        const double siPA = std::sin(PA);

        m.components.push_back(
            make_point("primaria",
                C + coPA * sep * 0.5, C + siPA * sep * 0.5,
                1.4, 1.0, SpectralModel::Stellar, 230e9));
        m.components.push_back(
            make_point("secondaria",
                C - coPA * sep * 0.5, C - siPA * sep * 0.5,
                1.4, q, SpectralModel::Stellar, 230e9));

        const double Mtot = rr(rng, 1.0, 4.0);            // draw 6
        m.physical.separation_au      = aAU;
        m.physical.distance_pc        = dPc;
        m.physical.flux_ratio         = q;
        m.physical.total_mass_solar   = Mtot;

        const double ph0 = rng.next() * k2Pi;              // draw 7
        m.temporal.model        = TemporalModel::Orbit;
        m.temporal.phase0       = ph0;
        m.temporal.period_years = std::sqrt(aAU * aAU * aAU / Mtot);

        m.dist_label  = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label  = "sep " + std::to_string(aAU).substr(0, 5) + " UA";
        m.extra_label = "rapporto flussi " + std::to_string(q).substr(0, 4);
    }
    else
    {
        // ── contact binary ───────────────────────────────────────────────────
        m.subtype = "contact_binary";
        m.fov_mul = 2.6;

        const double R1s = rr(rng, 20.0, 80.0);           // draw 3
        const double q   = rr(rng, 0.45, 0.95);           // draw 4
        const double dPc = rr(rng, 50.0, 400.0);          // draw 5

        const double sepAU = 0.85 * (R1s + q * R1s) * 0.00465;
        m.theta_obj = (sepAU / dPc) / 206264.806;

        const double sep  = static_cast<double>(N) / m.fov_mul;
        const double r1   = sep / (0.85 * (1.0 + q)) * 0.5;
        const double r2   = r1 * q;
        const double coPA = std::cos(PA);
        const double siPA = std::sin(PA);

        m.components.push_back(
            make_disk("lobo_a",
                C + coPA * sep * 0.5, C + siPA * sep * 0.5,
                r1, 0.18, PA, 0.5, 1.0, SpectralModel::Stellar, 230e9));
        m.components.push_back(
            make_disk("lobo_b",
                C - coPA * sep * 0.5, C - siPA * sep * 0.5,
                r2, 0.18, PA, 0.5, 0.85, SpectralModel::Stellar, 230e9));
        {
            Component bridge = make_gaussian("ponte",
                C, C, sep * 0.18, sep * 0.09, PA,
                0.5, SpectralModel::Stellar, 230e9);
            m.components.push_back(bridge);
        }

        m.physical.primary_radius_solar = R1s;
        m.physical.mass_ratio           = q;
        m.physical.distance_pc          = dPc;

        const double ph0    = rng.next() * k2Pi;           // draw 6
        const double period = rr(rng, 0.3, 2.0) / 365.0;  // draw 7
        m.temporal.model        = TemporalModel::Orbit;
        m.temporal.phase0       = ph0;
        m.temporal.period_years = period;

        m.dist_label  = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label  = "R\u2081 = " + std::to_string(static_cast<int>(R1s)) + " R\u2609";
        m.extra_label = "q = " + std::to_string(q).substr(0, 4);
        m.features.push_back("binaria a contatto");
        m.features.push_back("ponte di materia");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// STAR recipe
// Draw order: PA · roll · [subtype draws] [· env()]
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_star(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;  // draw 1
    const double roll = rng.next();       // draw 2

    // env() — ionised wind envelope; called at the end of some subtypes.
    // Consumes exactly ONE draw regardless of branch.
    // Captures m, C, rng, N by reference.
    auto env = [&]()
    {
        const double env_r = rng.next();  // env draw
        if (env_r < 0.35)
        {
            const double R2 = static_cast<double>(N) / m.fov_mul / 2.0;
            Component e = make_gaussian("inviluppo",
                C, C, R2 * 1.5, R2 * 1.5, 0.0,
                0.22, SpectralModel::FreeFree, 230e9);
            m.components.push_back(e);
            m.features.push_back("inviluppo ionizzato (vento)");
        }
    };

    if (roll < 0.30)
    {
        // ── supergiant classica ───────────────────────────────────────────────
        m.subtype = "supergiant";
        m.fov_mul = 2.2;

        const double Rsun = rr(rng, 80.0, 780.0);         // draw 3
        const double dPc  = rr(rng, 20.0, 420.0);         // draw 4
        const double ld   = rr(rng, 0.4, 0.8);            // draw 5
        m.theta_obj = (2.0 * Rsun * 0.00465 / dPc) / 206264.806;
        const double R = static_cast<double>(N) / m.fov_mul / 2.0;

        m.components.push_back(
            make_disk("fotosfera", C, C, R, 0.0, 0.0, ld, 1.0,
                SpectralModel::Stellar, 230e9));

        m.physical.radius_solar    = Rsun;
        m.physical.distance_pc     = dPc;
        m.physical.limb_darkening  = ld;

        const double ph0    = rng.next() * k2Pi;           // draw 6
        const double period = rr(rng, 0.5, 4.0);          // draw 7
        m.temporal.model        = TemporalModel::Rotation;
        m.temporal.phase0       = ph0;
        m.temporal.period_years = period;

        m.dist_label = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label = "R = " + std::to_string(static_cast<int>(Rsun)) + " R\u2609";
        env();
    }
    else if (roll < 0.45)
    {
        // ── oblate_star ───────────────────────────────────────────────────────
        m.subtype = "oblate_star";
        m.fov_mul = 2.2;

        const double Rsun = rr(rng, 80.0, 500.0);         // draw 3
        const double dPc  = rr(rng, 20.0, 320.0);         // draw 4
        const double ld   = rr(rng, 0.4, 0.8);            // draw 5
        const double e    = rr(rng, 0.16, 0.34);          // draw 6
        m.theta_obj = (2.0 * Rsun * 0.00465 / dPc) / 206264.806;

        const double R  = static_cast<double>(N) / m.fov_mul / 2.0;
        const double pr = R * (1.0 - e);
        const double siPA = std::sin(PA);
        const double coPA = std::cos(PA);

        m.components.push_back(
            make_disk("fotosfera", C, C, R, e, PA, ld, 1.0,
                SpectralModel::Stellar, 230e9));
        m.components.push_back(
            make_point("polo_n",
                C - siPA * pr * 0.72, C + coPA * pr * 0.72,
                R * 0.22, 0.3, SpectralModel::Stellar, 230e9));
        m.components.push_back(
            make_point("polo_s",
                C + siPA * pr * 0.72, C - coPA * pr * 0.72,
                R * 0.22, 0.3, SpectralModel::Stellar, 230e9));

        m.physical.radius_solar   = Rsun;
        m.physical.distance_pc    = dPc;
        m.physical.oblateness     = e;

        const double ph0    = rng.next() * k2Pi;           // draw 7
        const double period = rr(rng, 0.05, 0.5);         // draw 8
        m.temporal.model        = TemporalModel::Rotation;
        m.temporal.phase0       = ph0;
        m.temporal.period_years = period;

        m.dist_label  = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label  = "R = " + std::to_string(static_cast<int>(Rsun)) + " R\u2609";
        m.extra_label = "\u03b5 = " + std::to_string(e).substr(0, 4) + " (rotatore rapido)";
        m.features.push_back("oblata");
        m.features.push_back("oscuramento gravitazionale");
        env();
    }
    else if (roll < 0.60)
    {
        // ── brown dwarf with banded weather ───────────────────────────────────
        m.subtype = "brown_dwarf";
        m.fov_mul = 2.2;

        const double Rj  = rr(rng, 0.9, 1.15);             // draw 3
        const double dPc = rr(rng, 2.0, 12.0);             // draw 4
        const double T   = rr(rng, 900.0, 2200.0);         // draw 5
        m.theta_obj = (2.0 * Rj * 4.779e-4 / dPc) / 206264.806;
        const double R   = static_cast<double>(N) / m.fov_mul / 2.0;

        m.components.push_back(
            make_disk("fotosfera", C, C, R, 0.0, 0.0, 0.3, 1.0,
                SpectralModel::Stellar, 230e9));

        const double nb_d  = rng.next();                    // draw 6
        const int    nb    = 2 + static_cast<int>(nb_d * 2.0);
        const double siPA  = std::sin(PA);
        const double coPA  = std::cos(PA);
        for (int i = 0; i < nb; ++i)
        {
            const double off_frac = rr(rng, 0.2, 0.7);     // draw per band
            const double off = off_frac * ((i % 2) ? 1.0 : -1.0) * R;
            Component band;
            band.id = "banda_" + std::to_string(i + 1);
            band.type = PrimitiveType::Gaussian;
            band.x = C - siPA * off;
            band.y = C + coPA * off;
            band.sigma_x = R * 0.9;
            band.sigma_y = R * 0.13;
            band.angle = PA;
            band.flux = 0.22; band.flux_ref = 0.22;
            band.rot = true;
            band.spectral_model = SpectralModel::Stellar;
            band.reference_freq_hz = 230e9;
            m.components.push_back(band);
        }

        const double storm_r = rng.next();                  // storm roll
        if (storm_r < 0.7)
        {
            const double storm_a  = rng.next() * k2Pi;     // storm angle
            const double storm_rd = rng.next() * R * 0.55; // storm radius
            Component spot;
            spot.id = "macchia_1";
            spot.type = PrimitiveType::Point;
            spot.x = C + std::cos(storm_a) * storm_rd;
            spot.y = C + std::sin(storm_a) * storm_rd;
            spot.sigma = R * 0.18;
            spot.flux = 0.35; spot.flux_ref = 0.35;
            spot.rot = true;
            spot.spectral_model = SpectralModel::Stellar;
            spot.reference_freq_hz = 230e9;
            m.components.push_back(spot);
        }

        const double rotH = rr(rng, 2.0, 12.0);            // draw: rotH
        const double ph0  = rng.next() * k2Pi;             // draw: phase0
        m.physical.radius_jupiter   = Rj;
        m.physical.distance_pc      = dPc;
        m.physical.temp_k           = T;
        m.physical.rotation_hours   = rotH;
        m.temporal.model        = TemporalModel::Rotation;
        m.temporal.phase0       = ph0;
        m.temporal.period_years = rotH / 8766.0;

        m.dist_label = std::to_string(dPc).substr(0, 3) + " pc";
        m.phys_label = "R = " + std::to_string(Rj).substr(0, 4) + " R\u2c98 \u00b7 T \u2248 "
                     + std::to_string(static_cast<int>(T)) + " K";
        m.features.push_back("meteo a bande");
    }
    else if (roll < 0.75)
    {
        // ── T Tauri: inner disk + Herbig-Haro jet ─────────────────────────────
        m.subtype = "t_tauri";
        m.fov_mul = 2.6;

        const double RdAU = rr(rng, 0.5, 3.0);             // draw 3
        const double dPc  = rr(rng, 120.0, 170.0);         // draw 4
        m.theta_obj = (2.0 * RdAU / dPc) / 206264.806;

        const double R    = static_cast<double>(N) / m.fov_mul / 2.0;
        const double inc  = rr(rng, 15.0, 60.0) * std::numbers::pi / 180.0; // draw 5

        const int nk = 3;
        std::vector<JetKnot> knots;
        knots.reserve(nk);
        for (int i = 1; i <= nk; ++i)
        {
            const double t     = static_cast<double>(i) / nk;
            const double dAng  = (rng.next() - 0.5) * 0.15;  // draw per knot
            JetKnot k;
            k.t    = t;
            k.dAng = dAng;
            k.amp  = std::pow(1.0 - t, 1.1) * 0.6 + 0.08;
            k.sig  = 1.1 + t * 1.6;
            knots.push_back(k);
        }

        m.components.push_back(
            make_point("stella", C, C, 1.3, 1.0, SpectralModel::Stellar, 230e9));
        {
            HarmonicTerm dummy; // empty harm vector
            m.components.push_back(
                make_ring("disco_interno", C, C,
                    R * 0.55, std::max(1.2, R * 0.14),
                    1.0 - std::cos(inc), PA, {},
                    0.7, SpectralModel::ThermalDust, 230e9));
        }
        {
            Component jet;
            jet.id = "getto";
            jet.type = PrimitiveType::Jet;
            jet.x = C; jet.y = C;
            jet.length = R * 1.7;
            jet.angle = PA + std::numbers::pi / 2.0;
            jet.curvature = 0.0;
            jet.counter_jet_ratio = 0.8;
            jet.knots = std::move(knots);
            jet.flux = 0.5; jet.flux_ref = 0.5;
            jet.spectral_model = SpectralModel::FreeFree;
            jet.reference_freq_hz = 230e9;
            m.components.push_back(jet);
        }

        m.physical.inner_disk_au    = RdAU;
        m.physical.distance_pc      = dPc;
        m.physical.inclination_deg  = inc * 180.0 / std::numbers::pi;
        m.physical.jet_length_pc    = (1.7 * RdAU) / 206265.0;

        const double knotSpeed = rr(rng, 2e-4, 8e-4);     // draw after knots
        m.temporal.model       = TemporalModel::ProperMotion;
        m.temporal.knot_speed_c = knotSpeed;

        m.dist_label = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label = "disco " + std::to_string(RdAU).substr(0, 3) + " UA";
        m.features.push_back("getto Herbig-Haro");
        m.features.push_back("disco di accrescimento");
    }
    else if (roll < 0.875)
    {
        // ── Wolf-Rayet: dusty pinwheel spiral ─────────────────────────────────
        m.subtype = "wolf_rayet";
        m.fov_mul = 2.4;

        const double RspAU = rr(rng, 120.0, 350.0);        // draw 3
        const double dKpc  = rr(rng, 1.0, 3.0);           // draw 4
        const double P     = rr(rng, 0.5, 1.2);           // draw 5
        m.theta_obj = (2.0 * RspAU / (dKpc * 1000.0)) / 206264.806;
        const double R = static_cast<double>(N) / m.fov_mul / 2.0;

        m.components.push_back(
            make_point("stella", C, C, 1.3, 1.0, SpectralModel::Stellar, 230e9));

        constexpr int NSP = 10;
        for (int i = 0; i < NSP; ++i)
        {
            const double t   = static_cast<double>(i) / (NSP - 1);
            const double rpx = R * (0.15 + 0.85 * t);
            const double ang = PA + t * 4.0 * std::numbers::pi;
            const double spFlux = 0.7 * (1.0 - 0.6 * t) + 0.05;

            Component sp;
            sp.id = "spira_" + std::to_string(i + 1);
            sp.type = PrimitiveType::Point;
            sp.x = C + std::cos(ang) * rpx;
            sp.y = C + std::sin(ang) * rpx;
            sp.sigma = 1.2 + t * 2.2;
            sp.flux = spFlux; sp.flux_ref = spFlux;
            sp.spectral_model = SpectralModel::ThermalDust;
            sp.reference_freq_hz = 230e9;
            sp.orbit = Orbit{rpx, P, ang, 1.0, 0.0};
            m.components.push_back(sp);
        }

        m.physical.spiral_radius_au  = RspAU;
        m.physical.distance_kpc      = dKpc;
        m.physical.period_years      = P;
        m.temporal.model             = TemporalModel::MultiOrbit;
        m.temporal.outer_period_years = P;

        m.dist_label  = std::to_string(dKpc).substr(0, 3) + " kpc";
        m.phys_label  = "spirale " + std::to_string(static_cast<int>(RspAU)) + " UA";
        m.extra_label = "P = " + std::to_string(static_cast<int>(P * 365.25)) + " giorni";
        m.features.push_back("girandola di polvere (binaria WR)");
    }
    else
    {
        // ── dying supergiant: dust shells + convection cells ──────────────────
        m.subtype = "dying_supergiant";
        m.fov_mul = 2.8;

        const double Rsun   = rr(rng, 600.0, 1400.0);     // draw 3
        const double dPc    = rr(rng, 150.0, 700.0);      // draw 4
        const double shellF = rr(rng, 4.0, 7.0);          // draw 5
        m.theta_obj = (2.0 * Rsun * 0.00465 * shellF / dPc) / 206264.806;

        const double R  = static_cast<double>(N) / m.fov_mul / 2.0;
        const double Rp = R / shellF;

        m.components.push_back(
            make_disk("fotosfera", C, C, Rp, 0.0, 0.0, 0.6, 1.0,
                SpectralModel::Stellar, 230e9));

        // convection cells
        const double nc_d = rng.next();                    // draw 6
        const int    nc   = 2 + static_cast<int>(nc_d * 2.0);
        for (int i = 0; i < nc; ++i)
        {
            const double cell_a  = rng.next() * k2Pi;     // draw: angle
            const double cell_rd = rng.next() * Rp * 0.6; // draw: radius
            Component cell;
            cell.id = "cella_" + std::to_string(i + 1);
            cell.type = PrimitiveType::Point;
            cell.x = C + std::cos(cell_a) * cell_rd;
            cell.y = C + std::sin(cell_a) * cell_rd;
            cell.sigma = Rp * 0.35;
            cell.flux = 0.45; cell.flux_ref = 0.45;
            cell.rot = true;
            cell.spectral_model = SpectralModel::Stellar;
            cell.reference_freq_hz = 230e9;
            m.components.push_back(cell);
        }

        // dust shells — draw order per shell: ellipticity THEN harmonic phase
        // (matches JS: ellipticity:rr(rng,0,0.15) before ph:rng()*6.28)
        const double shell_params[2][2] = {{0.55, 0.35}, {0.95, 0.25}};
        for (int i = 0; i < 2; ++i)
        {
            const double ell = rr(rng, 0.0, 0.15);        // draw: ellipticity
            const double ph  = rng.next() * 6.28;          // draw: harmonic ph

            HarmonicTerm ht;
            ht.k  = 3 + i * 2;
            ht.A  = 0.35;
            ht.ph = ph;

            Component sh;
            sh.id = "guscio_" + std::to_string(i + 1);
            sh.type = PrimitiveType::Ring;
            sh.x = C; sh.y = C;
            sh.radius = R * shell_params[i][0];
            sh.width  = std::max(1.4, R * 0.08);
            sh.ellipticity = ell;
            sh.angle = PA;
            sh.harm.push_back(ht);
            sh.flux = shell_params[i][1];
            sh.flux_ref = shell_params[i][1];
            sh.spectral_model = SpectralModel::ThermalDust;
            sh.reference_freq_hz = 230e9;
            m.components.push_back(sh);
        }

        // optional SiO/H2O masers in inner shell
        const double maser_r = rng.next();                 // maser roll
        if (maser_r < 0.5)
        {
            const double line_d = rng.next();
            const double line   = (line_d < 0.5) ? 22.235e9 : 43.122e9;
            const int    nms    = 3;
            for (int i = 0; i < nms; ++i)
            {
                const double ma  = rng.next() * k2Pi;
                Component mas;
                mas.id   = "maser_" + std::to_string(i + 1);
                mas.type = PrimitiveType::Point;
                mas.x    = C + std::cos(ma) * R * 0.55;
                mas.y    = C + std::sin(ma) * R * 0.55;
                mas.sigma      = 1.1;
                mas.flux       = 0.4; mas.flux_ref = 0.4;
                mas.spectral_model   = SpectralModel::Maser;
                mas.line_freq_hz     = line;
                mas.maser_amp        = rr(rng, 25.0, 70.0);
                mas.reference_freq_hz = line;
                m.components.push_back(mas);
            }
            m.features.push_back("maser circumstellari");
        }

        m.physical.radius_solar    = Rsun;
        m.physical.distance_pc     = dPc;
        m.physical.shell_age_years = rr(rng, 800.0, 3000.0); // draw after masers

        m.temporal.model = TemporalModel::Expansion;

        m.dist_label  = std::to_string(static_cast<int>(dPc)) + " pc";
        m.phys_label  = "R = " + std::to_string(static_cast<int>(Rsun)) + " R\u2609";
        m.extra_label = "pre-supernova \u00b7 gusci a " + std::to_string(shellF).substr(0, 3) + " R\u2217";
        m.features.push_back("gusci di polvere espulsi");
        m.features.push_back("celle di convezione");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PROTO_DISK recipe
// Draw order: PA · RAU · dPc · inc · roll
//   classic_disk:    gap
//   transition_disk: r0_frac
//   multi_ring_disk: nr_frac
//   methanol branch: nm_frac · nm·(a+rd+amp)
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_proto_disk(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;             // draw 1

    const double RAU = rr(rng, 30.0, 180.0);         // draw 2
    const double dPc = rr(rng, 100.0, 450.0);        // draw 3
    m.theta_obj = (2.0 * RAU / dPc) / 206264.806;
    m.fov_mul = 2.6;

    const double inc = rr(rng, 20.0, 75.0) * std::numbers::pi / 180.0; // draw 4
    const double e   = 1.0 - std::cos(inc);
    const double R   = static_cast<double>(N) / m.fov_mul / 2.0;

    m.physical.disk_radius_au      = RAU;
    m.physical.distance_pc         = dPc;
    m.physical.inclination_deg     = inc * 180.0 / std::numbers::pi;
    m.physical.position_angle_deg  = PA * 180.0 / std::numbers::pi;
    m.temporal.model = TemporalModel::Static;

    m.dist_label  = std::to_string(static_cast<int>(dPc)) + " pc";
    m.phys_label  = "R = " + std::to_string(static_cast<int>(RAU)) + " UA";
    m.extra_label = "incl " + std::to_string(static_cast<int>(inc * 180.0 / std::numbers::pi)) + "\u00b0";

    Component star = make_point("stella", C, C, 1.3, 0.6, SpectralModel::Stellar, 230e9);

    const double roll = rng.next();                  // draw 5
    if (roll < 0.4)
    {
        // ── classic inclined disk ──────────────────────────────────────────────
        m.subtype = "classic_disk";
        const double gap  = rr(rng, 0.35, 0.65);    // draw 6
        const double r0   = R * (gap + 1.0) / 2.0;
        const double w    = R * (1.0 - gap) / 2.5;
        m.components.push_back(make_ring("anello", C, C, r0, w, e, PA, {}, 1.0,
            SpectralModel::ThermalDust, 230e9));
        m.components.push_back(star);
    }
    else if (roll < 0.7)
    {
        // ── transition disk ───────────────────────────────────────────────────
        m.subtype = "transition_disk";
        const double r0_frac = rr(rng, 0.6, 0.8);   // draw 6
        const double r0 = R * r0_frac;
        const double w  = R * 0.14;
        star.flux = 0.9; star.flux_ref = 0.9;
        m.components.push_back(make_ring("anello_esterno", C, C, r0, w, e, PA, {}, 1.0,
            SpectralModel::ThermalDust, 230e9));
        m.components.push_back(star);
        m.physical.cavity_au = RAU * r0 / R;
        m.features.push_back("cavit\u00e0 centrale ampia");
    }
    else
    {
        // ── multi-ring disk ───────────────────────────────────────────────────
        m.subtype = "multi_ring_disk";
        const double nr_d = rng.next();              // draw 6
        const int    nr   = 2 + (nr_d < 0.5 ? 0 : 1);
        const double radii[3] = {0.38, 0.66, 0.94};
        for (int i = 0; i < nr; ++i)
        {
            const double rf = 1.0 - 0.22 * i;
            m.components.push_back(make_ring(
                "anello_" + std::to_string(i + 1),
                C, C, R * radii[i], R * 0.06 + 0.8, e, PA, {}, rf,
                SpectralModel::ThermalDust, 230e9));
        }
        m.components.push_back(star);
        m.physical.ring_count = nr;
        m.features.push_back(std::to_string(nr) + " anelli concentrici");
    }

    // methanol maser branch
    const double meth_r = rng.next();               // draw: mether roll
    if (meth_r < 0.3)
    {
        const double R3   = static_cast<double>(N) / m.fov_mul / 2.0;
        const double nm_d = rng.next();
        const int    nm   = 2 + static_cast<int>(nm_d * 3.0);
        const double cos_inc = std::cos(inc);
        for (int i = 0; i < nm; ++i)
        {
            const double ma  = rng.next() * k2Pi;
            const double mrd = rr(rng, 0.2, 0.5) * R3;
            Component mas;
            mas.id   = "maser_" + std::to_string(i + 1);
            mas.type = PrimitiveType::Point;
            mas.x    = C + std::cos(ma) * mrd;
            mas.y    = C + std::sin(ma) * mrd * cos_inc;
            mas.sigma = 1.1;
            mas.flux  = 0.4; mas.flux_ref = 0.4;
            mas.spectral_model   = SpectralModel::Maser;
            mas.line_freq_hz     = 6.668e9;
            mas.maser_amp        = rr(rng, 25.0, 70.0);
            mas.reference_freq_hz = 6.668e9;
            m.components.push_back(mas);
        }
        m.features.push_back(std::to_string(nm) + " maser CH\u2083OH");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NOVA recipe
// Draw order: PA · branch(<0.25 → pulsar, else nova_shell)
//   pulsar:    Rpc·dKpc·ell_neb·ph_neb·periodMs·dmPcCm3
//   nova_shell: RAU·dKpc·ell·k1·ph1·k2·ph2·vExp·maser_roll[·ns·line·ns·(a+amp)]
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_nova(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;             // draw 1

    if (rng.next() < 0.25)                            // draw 2 — branch
    {
        // ── pulsar + wind nebula ───────────────────────────────────────────────
        m.subtype = "pulsar";
        m.fov_mul = 2.4;

        const double Rpc  = rr(rng, 0.05, 0.3);     // draw 3
        const double dKpc = rr(rng, 1.5, 5.0);      // draw 4
        m.theta_obj = (2.0 * Rpc) / (dKpc * 1000.0);
        const double R = static_cast<double>(N) / m.fov_mul / 2.0;

        m.components.push_back(
            make_point("pulsar", C, C, 1.2, 1.0, SpectralModel::Synchrotron, 230e9, -1.6));

        const double ell_neb = rr(rng, 0.0, 0.3);   // draw 5
        const double ph_neb  = rng.next() * k2Pi;    // draw 6
        {
            HarmonicTerm ht; ht.k = 2; ht.A = 0.3; ht.ph = ph_neb;
            m.components.push_back(
                make_ring("nebulosa_vento", C, C,
                    R * 0.72, R * 0.26, ell_neb, PA, {ht},
                    0.5, SpectralModel::Synchrotron, 230e9, -0.5));
        }

        const double periodMs = std::pow(10.0, rr(rng, 0.2, 3.3)); // draw 7
        const double dmPcCm3  = rr(rng, 20.0, 300.0);              // draw 8
        m.physical.nebula_radius_pc = Rpc;
        m.physical.distance_kpc    = dKpc;
        m.physical.period_ms       = periodMs;
        m.physical.dm_pc_cm3       = dmPcCm3;
        m.temporal.model = TemporalModel::Static;

        m.dist_label  = std::to_string(dKpc).substr(0, 3) + " kpc";
        m.phys_label  = "P = " + std::to_string(periodMs).substr(0, 5) + " ms";
        m.extra_label = "DM " + std::to_string(static_cast<int>(dmPcCm3)) + " pc cm\u207b\u00b3";
        m.features.push_back("impulsi coerenti");
        m.features.push_back("nebulosa di vento");
        return;
    }

    // ── nova shell ─────────────────────────────────────────────────────────────
    m.subtype = "nova_shell";
    m.fov_mul = 2.6;

    const double RAU  = rr(rng, 500.0, 3500.0);     // draw 3
    const double dKpc = rr(rng, 0.8, 4.3);          // draw 4
    m.theta_obj = (2.0 * RAU / (dKpc * 1000.0)) / 206264.806;
    const double R = static_cast<double>(N) / m.fov_mul / 2.0;

    const double ell  = rr(rng, 0.0, 0.2);          // draw 5
    const double w    = R * 0.07 + 1.0;

    // harmonics: k1 · ph1 · k2 · ph2 (4 draws, must bind each)
    const double k1_d = rng.next();                  // draw 6
    const double ph1  = rng.next() * k2Pi;           // draw 7
    const double k2_d = rng.next();                  // draw 8
    const double ph2  = rng.next() * k2Pi;           // draw 9
    {
        const int k1 = 2 + static_cast<int>(std::floor(k1_d * 4.0));
        const int k2 = 5 + static_cast<int>(std::floor(k2_d * 6.0));
        HarmonicTerm h1; h1.k = k1; h1.A = 0.45; h1.ph = ph1;
        HarmonicTerm h2; h2.k = k2; h2.A = 0.30; h2.ph = ph2;
        m.components.push_back(
            make_ring("guscio", C, C, R, w, ell, PA, {h1, h2},
                1.0, SpectralModel::FreeFree, 230e9));
    }
    m.components.push_back(
        make_point("residuo_centrale", C, C, 1.2, 0.35, SpectralModel::Stellar, 230e9));

    const double vExp = rr(rng, 300.0, 2500.0);     // draw 10
    m.physical.shell_radius_au = RAU;
    m.physical.distance_kpc    = dKpc;
    m.physical.expansion_kms   = vExp;
    m.physical.age_years       = RAU * 1.496e8 / (vExp * 3.156e7);

    // maser spots on shell
    const double maser_r = rng.next();               // draw 11
    if (maser_r < 0.4)
    {
        const double ns_d   = rng.next();            // draw: ns
        const int    ns     = 3 + static_cast<int>(std::floor(ns_d * 3.0));
        const double line_d = rng.next();            // draw: line
        const double line   = (line_d < 0.6) ? 22.235e9 : 43.122e9;
        const double coPA   = std::cos(PA);
        const double siPA   = std::sin(PA);
        for (int i = 0; i < ns; ++i)
        {
            const double ma = rng.next() * k2Pi;
            const double lx = std::cos(ma) * R;
            const double ly = std::sin(ma) * R * (1.0 - ell);
            Component mas;
            mas.id   = "maser_" + std::to_string(i + 1);
            mas.type = PrimitiveType::Point;
            mas.x    = C + lx * coPA - ly * siPA;
            mas.y    = C + lx * siPA + ly * coPA;
            mas.sigma = 1.1;
            mas.flux  = 0.5; mas.flux_ref = 0.5;
            mas.spectral_model   = SpectralModel::Maser;
            mas.line_freq_hz     = line;
            mas.maser_amp        = rr(rng, 25.0, 80.0);
            mas.reference_freq_hz = line;
            m.components.push_back(mas);
        }
        m.features.push_back(std::to_string(ns) + " spot maser "
            + (line > 3e10 ? "SiO" : "H\u2082O"));
    }

    m.temporal.model             = TemporalModel::Expansion;
    m.temporal.rate_px_per_year  = 0.02;
    m.dist_label  = std::to_string(dKpc).substr(0, 3) + " kpc";
    m.phys_label  = "R = " + std::to_string(static_cast<int>(RAU)) + " UA";
    m.extra_label = "v\u2091 \u2248 " + std::to_string(static_cast<int>(vExp)) + " km/s";
}

// ─────────────────────────────────────────────────────────────────────────────
// AGN recipe
// Draw order: PA · branch(<0.62 → core_jet, else double_lobe)
//   core_jet:   Lpc·dMpc·nb_frac·nb·knot_dAng·aCore·aJet·knotSpeed
//   double_lobe: Lkpc·dMpc·lr_frac·asym·aLobe·aHS·aCore
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_agn(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;             // draw 1

    if (rng.next() < 0.62)                            // draw 2 — branch
    {
        // ── core + jet ────────────────────────────────────────────────────────
        m.subtype = "core_jet";
        m.fov_mul = 2.4;

        const double Lpc  = rr(rng, 2.0, 32.0);     // draw 3
        const double dMpc = rr(rng, 10.0, 210.0);   // draw 4
        m.theta_obj = Lpc / (dMpc * 1e6);

        const double L   = static_cast<double>(N) / m.fov_mul;
        const double cx  = C - std::cos(PA) * L * 0.35;
        const double cy  = C - std::sin(PA) * L * 0.35;
        const double nb_d = rng.next();              // draw 5
        const int    nb  = 4 + static_cast<int>(nb_d * 5.0);

        std::vector<JetKnot> knots;
        knots.reserve(nb);
        for (int i = 1; i <= nb; ++i)
        {
            const double t    = static_cast<double>(i) / nb;
            const double dAng = (rng.next() - 0.5) * 0.25; // draw per knot
            JetKnot k;
            k.t    = t;
            k.dAng = dAng;
            k.amp  = std::pow(1.0 - t, 1.6) * 0.7 + 0.03;
            k.sig  = 1.2 + t * 3.2;
            knots.push_back(k);
        }

        const double aCore = rr(rng, -0.2, 0.1);    // draw after knots
        const double aJet  = rr(rng, -1.0, -0.6);

        m.components.push_back(
            make_point("core", cx, cy, 1.5, 1.0,
                SpectralModel::Synchrotron, 230e9, aCore, -0.25));
        {
            Component jet;
            jet.id = "getto";
            jet.type = PrimitiveType::Jet;
            jet.x = cx; jet.y = cy;
            jet.length = L;
            jet.angle = PA;
            jet.curvature = 0.0;
            jet.counter_jet_ratio = 0.0;
            jet.knots = std::move(knots);
            jet.flux = 1.0; jet.flux_ref = 1.0;
            jet.spectral_model = SpectralModel::Synchrotron;
            jet.alpha = aJet;
            jet.reference_freq_hz = 230e9;
            m.components.push_back(jet);
        }

        m.physical.core_spectral_index = aCore;
        m.physical.jet_spectral_index  = aJet;
        m.physical.jet_length_pc       = Lpc;
        m.physical.distance_mpc        = dMpc;
        m.physical.knot_count          = static_cast<double>(nb);
        m.physical.jet_pa_deg          = PA * 180.0 / std::numbers::pi;

        const double knotSpeed = rr(rng, 0.9, 8.0);
        m.temporal.model        = TemporalModel::ProperMotion;
        m.temporal.knot_speed_c = knotSpeed;

        m.dist_label  = std::to_string(static_cast<int>(dMpc)) + " Mpc";
        m.phys_label  = "getto " + std::to_string(Lpc).substr(0, 4) + " pc";
        m.extra_label = "componenti " + std::to_string(nb + 1);
    }
    else
    {
        // ── double-lobe radio galaxy ───────────────────────────────────────────
        m.subtype = "double_lobe";
        m.fov_mul = 1.7;

        const double Lkpc = rr(rng, 60.0, 380.0);   // draw 3
        const double dMpc = rr(rng, 60.0, 820.0);   // draw 4
        m.theta_obj = Lkpc / (dMpc * 1000.0);

        const double L    = static_cast<double>(N) / m.fov_mul;
        const double lr_f = rr(rng, 0.10, 0.16);    // draw 5
        const double lr   = L * lr_f;
        const double asym = rr(rng, 0.75, 1.0);     // draw 6
        const double aLobe = rr(rng, -1.2, -0.8);   // draw 7
        const double aHS   = rr(rng, -0.7, -0.5);   // draw 8
        const double aCore = rr(rng, -0.2, 0.1);    // draw 9

        const double coPA = std::cos(PA);
        const double siPA = std::sin(PA);

        m.components.push_back(
            make_point("core", C, C, 1.3, 0.35, SpectralModel::Synchrotron, 230e9, aCore));
        m.components.push_back(
            make_gaussian("lobo_a",
                C + coPA * L * 0.33, C + siPA * L * 0.33,
                lr * 1.35, lr, PA,
                0.8, SpectralModel::Synchrotron, 230e9, aLobe));
        m.components.push_back(
            make_gaussian("lobo_b",
                C - coPA * L * 0.33 * asym, C - siPA * L * 0.33 * asym,
                lr * 1.35, lr, PA,
                0.8 * asym, SpectralModel::Synchrotron, 230e9, aLobe));
        m.components.push_back(
            make_point("hotspot_a",
                C + coPA * L * 0.44, C + siPA * L * 0.44,
                1.6, 0.9, SpectralModel::Synchrotron, 230e9, aHS));
        m.components.push_back(
            make_point("hotspot_b",
                C - coPA * L * 0.44 * asym, C - siPA * L * 0.44 * asym,
                1.6, 0.9 * asym, SpectralModel::Synchrotron, 230e9, aHS));

        m.physical.lobe_span_kpc  = Lkpc;
        m.physical.distance_mpc   = dMpc;
        m.physical.lobe_asymmetry = asym;
        m.temporal.model = TemporalModel::Static;

        m.dist_label  = std::to_string(static_cast<int>(dMpc)) + " Mpc";
        m.phys_label  = "estensione " + std::to_string(static_cast<int>(Lkpc)) + " kpc";
        m.extra_label = "FR II";
        m.features.push_back("doppio lobo");
        m.features.push_back("hotspot terminali");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// COMPACT recipe — BH crescent (single subtype)
// Draw order: PA · thUas · dMpc · A · alpha_ring
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_compact(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;             // draw 1

    m.subtype = "bh_crescent";
    m.fov_mul = 2.6;

    const double thUas = rr(rng, 25.0, 90.0);        // draw 2
    const double dMpc  = rr(rng, 5.0, 60.0);         // draw 3
    const double M9    = thUas / 42.0 * 6.5 * (dMpc / 16.8);
    m.theta_obj = thUas * 1e-6 / 206264.806;

    const double R   = static_cast<double>(N) / m.fov_mul / 2.0;
    const double w   = R * 0.16 + 0.8;
    const double A   = rr(rng, 0.4, 0.9);            // draw 4
    const double alp = rr(rng, 0.1, 0.5);            // draw 5

    {
        HarmonicTerm ht; ht.k = 1; ht.A = A; ht.ph = PA;
        m.components.push_back(
            make_ring("anello_fotonico", C, C, R, w, 0.0, 0.0, {ht},
                1.0, SpectralModel::Synchrotron, 230e9, alp));
    }
    m.components.push_back(
        make_disk("emissione_interna", C, C, R * 0.7, 0.0, 0.0, 0.0, 0.05,
            SpectralModel::Synchrotron, 230e9));

    m.physical.ring_diameter_uas  = thUas;
    m.physical.distance_mpc       = dMpc;
    m.physical.mass_billion_solar = M9;
    m.physical.doppler_asymmetry  = A;
    m.temporal.model = TemporalModel::Static;

    m.dist_label  = std::to_string(dMpc).substr(0, 4) + " Mpc";
    m.phys_label  = "M \u2248 " + std::to_string(M9).substr(0, 4) + " \u00d710\u2079 M\u2609";
    m.extra_label = "asimmetria Doppler " + std::to_string(A).substr(0, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// PLANETARY recipe
// Draw order: PA · dPc · Mstar · aOut
//   sculpted_disk: inc · rIn_frac · np_frac · np·(aA_frac+cst+phase0)
//   young_system:  cosI_inc · np_frac · ratio · np·(cst+phase0)
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_planetary(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C  = static_cast<double>(N) / 2.0;
    const double PA = rng.next() * k2Pi;             // draw 1

    const double dPc   = rr(rng, 10.0, 60.0);       // draw 2
    const double Mstar = rr(rng, 0.7, 2.2);          // draw 3
    const double aOut  = rr(rng, 8.0, 45.0);         // draw 4
    m.theta_obj = (2.0 * aOut / dPc) / 206264.806;
    m.fov_mul   = 2.4;

    const double pxPerAU = (static_cast<double>(N) / m.fov_mul / 2.0) / aOut;

    m.dist_label = std::to_string(static_cast<int>(dPc)) + " pc";

    Component star = make_point("stella", C, C, 1.4, 1.0, SpectralModel::Stellar, 230e9);

    // mkPlanet lambda: consumes exactly 1 draw (phase0)
    auto mk_planet = [&](int idx, double aAU, double contrast,
                          double cosI, SpectralModel spec, double periodY) -> Component
    {
        const double phase0 = rng.next() * k2Pi;     // draw: phase0
        Orbit orb;
        orb.aPx         = aAU * pxPerAU;
        orb.periodYears = periodY;
        orb.phase0      = phase0;
        orb.cosI        = cosI;
        orb.pa          = PA;

        const double lx = orb.aPx * std::cos(phase0);
        const double ly = orb.aPx * std::sin(phase0) * cosI;
        const double px = C + lx * std::cos(PA) - ly * std::sin(PA);
        const double py = C + lx * std::sin(PA) + ly * std::cos(PA);

        char name_buf[2] = {static_cast<char>('b' + idx), '\0'};
        Component p = make_point("pianeta_" + std::string(name_buf),
            px, py, 1.2, contrast, spec, 230e9);
        p.flux_ref = contrast;
        p.orbit = orb;
        return p;
    };

    std::vector<double> contrasts;

    if (rng.next() < 0.55)                            // draw 5 — branch
    {
        // ── sculpted disk (PDS70-style) ────────────────────────────────────────
        m.subtype = "sculpted_disk";

        const double inc  = rr(rng, 10.0, 55.0) * std::numbers::pi / 180.0; // draw 6
        const double cosI = std::cos(inc);
        const double e    = 1.0 - cosI;
        const double rIn  = rr(rng, 0.25, 0.4) * aOut;                      // draw 7

        m.components.push_back(star);
        m.components.push_back(make_ring("anello_interno", C, C,
            rIn * pxPerAU, std::max(1.2, 0.08 * aOut * pxPerAU), e, PA, {},
            0.8, SpectralModel::ThermalDust, 230e9));
        m.components.push_back(make_ring("anello_esterno", C, C,
            aOut * pxPerAU, std::max(1.4, 0.1 * aOut * pxPerAU), e, PA, {},
            1.0, SpectralModel::ThermalDust, 230e9));

        const double np_d = rng.next();              // draw 8
        const int    np   = 1 + (np_d < 0.4 ? 1 : 0);
        for (int i = 0; i < np; ++i)
        {
            const double aA_d = rng.next();          // draw: aA
            const double aA   = (i == 0 ? (0.52 + aA_d * 0.20) : (0.44 + aA_d * 0.06)) * aOut;
            const double cst  = rr(rng, 3e-3, 2e-2); // draw: contrast
            const double pY   = std::sqrt(aA * aA * aA / Mstar);
            contrasts.push_back(cst);
            m.components.push_back(mk_planet(i, aA, cst, cosI, SpectralModel::ThermalDust, pY));
        }

        m.physical.distance_pc       = dPc;
        m.physical.star_mass_solar   = Mstar;
        m.physical.outer_ring_au     = aOut;
        m.physical.planet_count      = np;
        m.physical.inclination_deg   = inc * 180.0 / std::numbers::pi;
        m.phys_label = "anello " + std::to_string(static_cast<int>(aOut)) + " UA \u00b7 "
                     + std::to_string(np) + " pianeti";
        m.features.push_back("pianeti nei gap del disco");
    }
    else
    {
        // ── young multi-planet system ──────────────────────────────────────────
        m.subtype = "young_system";

        const double inc  = rr(rng, 0.0, 40.0) * std::numbers::pi / 180.0; // draw 6
        const double cosI = std::cos(inc);
        const double np_d = rng.next();              // draw 7
        const int    np   = 2 + static_cast<int>(np_d * 3.0);
        const double ratio = rr(rng, 1.5, 1.9);     // draw 8

        double aA = aOut / std::pow(ratio, static_cast<double>(np - 1));
        m.components.push_back(star);
        for (int i = 0; i < np; ++i)
        {
            const double cst = rr(rng, 3e-4, 1.5e-3); // draw: contrast
            const double pY  = std::sqrt(aA * aA * aA / Mstar);
            contrasts.push_back(cst);
            m.components.push_back(mk_planet(i, aA, cst, cosI, SpectralModel::Stellar, pY));
            aA *= ratio;
        }

        m.physical.distance_pc     = dPc;
        m.physical.star_mass_solar = Mstar;
        m.physical.outermost_au    = aOut;
        m.physical.planet_count    = np;
        m.phys_label = std::to_string(np) + " pianeti \u00b7 a\u2098\u2090\u2093 "
                     + std::to_string(static_cast<int>(aOut)) + " UA";
        m.features.push_back("architettura risonante");
    }

    m.temporal.model             = TemporalModel::MultiOrbit;
    m.temporal.outer_period_years = std::sqrt(aOut * aOut * aOut / Mstar);

    if (!contrasts.empty())
    {
        const double cmin = *std::min_element(contrasts.begin(), contrasts.end());
        m.extra_label = "contrasto min 1:" + std::to_string(static_cast<int>(1.0 / cmin));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PLANET_RES recipe
// Draw order: ki_frac · Re(giant or not) · dPc · rotH(giant or not) ·
//             nz · rotPhase · cloudPhase · phaseAngle · seaLevel · capLat ·
//             cloudAmt[0 draws if giant, 1 otherwise]
// ─────────────────────────────────────────────────────────────────────────────
static void recipe_planet_res(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double C = static_cast<double>(N) / 2.0;

    const double ki_d  = rng.next();                 // draw 1
    const int    ki    = static_cast<int>(std::floor(ki_d * 4.0));
    const std::string kinds[4] = {"planet_ocean", "planet_arid",
                                   "planet_giant", "planet_ice"};
    m.subtype = kinds[ki];
    const bool giant = (ki == 2);

    const double Re   = giant ? rr(rng, 9.0, 12.0) : rr(rng, 0.7, 1.8);  // draw 2
    const double dPc  = rr(rng, 2.0, 15.0);          // draw 3
    m.theta_obj = (2.0 * Re * 4.2635e-5 / dPc) / 206264.806;
    m.fov_mul = 2.0;

    const double R    = static_cast<double>(N) / m.fov_mul / 2.0;
    const double rotH = giant ? rr(rng, 8.0, 14.0) : rr(rng, 16.0, 40.0); // draw 4

    const double nz_d       = rng.next();             // draw 5
    const double rotPhase   = rng.next() * k2Pi;      // draw 6
    const double cloudPhase = rng.next() * k2Pi;      // draw 7
    const double phaseAngle = rr(rng, 0.25, 1.0);    // draw 8
    const double seaLevel   = rr(rng, 0.35, 0.6);    // draw 9
    const double capLat     = rr(rng, 0.9, 1.25);    // draw 10

    double cloudAmt = 0.0;
    if (ki == 1)
    {
        cloudAmt = rr(rng, 0.05, 0.2);               // draw 11 (arid)
    }
    else if (ki == 2)
    {
        cloudAmt = 0.0;                               // no draw (giant)
    }
    else
    {
        cloudAmt = rr(rng, 0.3, 0.7);                // draw 11 (ocean/ice)
    }

    Component surf;
    surf.id   = "superficie";
    surf.type = PrimitiveType::PlanetSurface;
    surf.x = C; surf.y = C;
    surf.radius     = R;
    surf.kind       = m.subtype;
    surf.nz         = static_cast<int>(std::floor(nz_d * 1e9));
    surf.rot_phase  = rotPhase;
    surf.cloud_phase = cloudPhase;
    surf.phase_angle = phaseAngle;
    surf.sea_level  = seaLevel;
    surf.cap_lat    = capLat;
    surf.cloud_amt  = cloudAmt;
    surf.flux       = 1.0; surf.flux_ref = 1.0;
    surf.spectral_model = SpectralModel::Stellar;
    surf.reference_freq_hz = 230e9;
    m.components.push_back(surf);

    m.physical.radius_earth    = Re;
    m.physical.distance_pc     = dPc;
    m.physical.rotation_hours  = rotH;
    m.physical.surface_type    = m.subtype;
    m.temporal.model           = TemporalModel::PlanetRotation;
    m.temporal.period_years    = rotH / 8766.0;

    m.dist_label  = std::to_string(dPc).substr(0, 4) + " pc";
    m.phys_label  = "R = " + std::to_string(Re).substr(0, 4) + " R\u2295 \u00b7 rot "
                  + std::to_string(static_cast<int>(rotH)) + " h";
    m.extra_label = subtype_label(m.subtype);
    for (auto& c : m.extra_label) c = static_cast<char>(std::tolower(c));
    m.features.push_back("stella madre fuori campo");
    m.features.push_back("fase di illuminazione");
}

// ─────────────────────────────────────────────────────────────────────────────
// Modifiers
// ─────────────────────────────────────────────────────────────────────────────

static bool mod_hotspot(TargetModel& m, Rng& rng)
{
    Component* ring = nullptr;
    Component* disk = nullptr;
    for (auto& c : m.components)
    {
        if (!ring && c.type == PrimitiveType::Ring) ring = &c;
        if (!disk  && c.type == PrimitiveType::Disk) disk = &c;
    }
    const double ang = rng.next() * k2Pi;  // draw always
    if (ring)
    {
        const double coAng = std::cos(ring->angle);
        const double siAng = std::sin(ring->angle);
        const double lx  = std::cos(ang) * ring->radius;
        const double ly  = std::sin(ang) * ring->radius * (1.0 - ring->ellipticity);
        const double fhs = rr(rng, 0.35, 0.75);
        Component hs;
        hs.id   = "hotspot";
        hs.type = PrimitiveType::Point;
        hs.x    = ring->x + lx * coAng - ly * siAng;
        hs.y    = ring->y + lx * siAng + ly * coAng;
        hs.sigma = std::max(1.2, ring->width * 0.8);
        hs.flux = fhs; hs.flux_ref = fhs;
        hs.spectral_model   = ring->spectral_model;
        hs.alpha            = ring->alpha;
        hs.reference_freq_hz = 230e9;
        m.components.push_back(hs);
        return true;
    }
    if (disk)
    {
        const double rd  = rng.next() * disk->radius * 0.6;
        const double fhs = rr(rng, 0.5, 1.0);
        Component hs;
        hs.id   = "hotspot";
        hs.type = PrimitiveType::Point;
        hs.x    = disk->x + std::cos(ang) * rd;
        hs.y    = disk->y + std::sin(ang) * rd;
        hs.sigma = disk->radius * 0.16;
        hs.flux = fhs; hs.flux_ref = fhs;
        hs.rot = true;
        hs.spectral_model = SpectralModel::Stellar;
        hs.reference_freq_hz = 230e9;
        m.components.push_back(hs);
        return true;
    }
    return false;
}

static bool mod_companion(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double ang = rng.next() * k2Pi;
    const double rd  = rr(rng, 0.30, 0.44) * static_cast<double>(N);
    const double fc  = rr(rng, 0.05, 0.16);
    const double Chalf = static_cast<double>(N) / 2.0;
    Component comp;
    comp.id   = "compagna";
    comp.type = PrimitiveType::Point;
    comp.x    = Chalf + std::cos(ang) * rd;
    comp.y    = Chalf + std::sin(ang) * rd;
    comp.sigma = 1.3;
    comp.flux  = fc; comp.flux_ref = fc;
    comp.spectral_model = SpectralModel::Stellar;
    comp.reference_freq_hz = 230e9;
    m.components.push_back(comp);
    return true;
}

static bool mod_asymmetric(TargetModel& m, Rng& rng)
{
    Component* ring = nullptr;
    for (auto& c : m.components)
        if (c.type == PrimitiveType::Ring) { ring = &c; break; }
    if (ring)
    {
        const double A  = rr(rng, 0.4, 0.8);
        const double ph = rng.next() * k2Pi;
        HarmonicTerm ht; ht.k = 1; ht.A = A; ht.ph = ph;
        ring->harm.push_back(ht);
    }
    else
    {
        if (!m.components.empty())
            m.components[0].flux *= 1.4;
    }
    return true;
}

static bool mod_clumpy(TargetModel& m, Rng& rng)
{
    Component* ring = nullptr;
    for (auto& c : m.components)
        if (c.type == PrimitiveType::Ring) { ring = &c; break; }
    if (!ring) return false;

    const double k1_d = rng.next();
    const double ph1  = rng.next() * k2Pi;
    const double k2_d = rng.next();
    const double ph2  = rng.next() * k2Pi;
    HarmonicTerm h1; h1.k = 5 + static_cast<int>(k1_d * 5.0); h1.A = 0.35; h1.ph = ph1;
    HarmonicTerm h2; h2.k = 8 + static_cast<int>(k2_d * 6.0); h2.A = 0.25; h2.ph = ph2;
    ring->harm.push_back(h1);
    ring->harm.push_back(h2);
    return true;
}

static bool mod_central_cavity(TargetModel& m, Rng& rng, std::uint32_t N)
{
    Component* ring = nullptr;
    for (auto& c : m.components)
        if (c.type == PrimitiveType::Ring) { ring = &c; break; }

    const double cavR = ring
        ? ring->radius * rr(rng, 0.3, 0.5)
        : static_cast<double>(N) / 8.0;
    const double Chalf = static_cast<double>(N) / 2.0;

    Component cav;
    cav.id    = "cavita";
    cav.type  = PrimitiveType::Absorption;
    cav.shape = "disk";
    cav.x     = Chalf;
    cav.y     = Chalf;
    cav.radius = cavR;
    cav.depth  = 0.85;
    m.components.push_back(cav);
    return true;
}

static bool mod_obscured(TargetModel& m, Rng& rng, std::uint32_t N)
{
    const double ang   = rng.next() * std::numbers::pi;
    const double width = static_cast<double>(N) * rr(rng, 0.045, 0.09);
    const double depth = rr(rng, 0.55, 0.9);
    const double Chalf = static_cast<double>(N) / 2.0;

    Component ob;
    ob.id    = "corsia_polvere";
    ob.type  = PrimitiveType::Absorption;
    ob.shape = "band";
    ob.x     = Chalf;
    ob.y     = Chalf;
    ob.angle = ang;
    ob.width = width;
    ob.depth = depth;
    m.components.push_back(ob);
    return true;
}

static bool mod_counterjet(TargetModel& m, Rng& rng)
{
    Component* jet = nullptr;
    for (auto& c : m.components)
        if (c.type == PrimitiveType::Jet) { jet = &c; break; }
    if (!jet) return false;
    jet->counter_jet_ratio = rr(rng, 0.15, 0.4);
    return true;
}

static bool mod_precessing(TargetModel& m, Rng& rng)
{
    Component* jet = nullptr;
    for (auto& c : m.components)
        if (c.type == PrimitiveType::Jet) { jet = &c; break; }
    if (!jet) return false;
    // Two draws: magnitude then direction (bind both in source order)
    const double d1 = rr(rng, 0.2, 0.5);     // draw: magnitude
    const double d2 = rng.next();             // draw: sign
    jet->curvature = d1 * (d2 < 0.5 ? -1.0 : 1.0);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// applyCompatibleModifiers
// ─────────────────────────────────────────────────────────────────────────────
// Draw order (BINDING):
//   1. rarity_roll (sets rarity)
//   2. budget_draw (one draw always, value used only for COMMON)
//   3. capRoll (sets cap for Free complexity)
//   4. Fisher-Yates shuffle: i from (cand.size()-1) down to 1, 1 draw each
//   5. internal draws of each applied modifier (conditional on return value)

static const char* const kModifierNames[] = {
    "HOTSPOT", "COMPANION", "ASYMMETRIC", "CLUMPY",
    "CENTRAL_CAVITY", "OBSCURED", "COUNTERJET", "PRECESSING"
};
static const char* const kFeatureLabels[] = {
    "hotspot", "compagna debole", "asimmetria", "frammentazione",
    "cavit\u00e0 centrale", "corsia di polvere", "controgetto", "getto precessionante"
};

// Returns the compatible modifier names for a given family (as indices into kModifierNames)
static std::vector<int> compatible_modifiers(Family f)
{
    switch (f)
    {
    case Family::Binary:
        return {1, 2, 5}; // COMPANION ASYMMETRIC OBSCURED
    case Family::Star:
        return {0, 1, 2, 5}; // HOTSPOT COMPANION ASYMMETRIC OBSCURED
    case Family::ProtoDisk:
        return {0, 3, 4, 2, 1, 5}; // HOTSPOT CLUMPY CENTRAL_CAVITY ASYMMETRIC COMPANION OBSCURED
    case Family::Nova:
        return {3, 2, 5, 1}; // CLUMPY ASYMMETRIC OBSCURED COMPANION
    case Family::Agn:
        return {0, 6, 7, 5}; // HOTSPOT COUNTERJET PRECESSING OBSCURED
    case Family::Compact:
        return {0, 2}; // HOTSPOT ASYMMETRIC
    case Family::Planetary:
        return {3, 2, 5, 1}; // CLUMPY ASYMMETRIC OBSCURED COMPANION
    case Family::PlanetRes:
        return {1}; // COMPANION
    default:
        return {};
    }
}

void apply_compatible_modifiers(TargetModel& m, Rng& rng,
                                 Complexity complexity, std::uint32_t N)
{
    // Step 1: rarity roll
    const double roll = rng.next();
    Rarity rarity;
    if      (roll < 0.002) rarity = Rarity::Exceptional;
    else if (roll < 0.015) rarity = Rarity::Rare;
    else if (roll < 0.06 ) rarity = Rarity::Uncommon;
    else                   rarity = Rarity::Common;
    m.rarity = rarity;

    // Step 2: budget draw (ALWAYS one draw consumed)
    int budget;
    if (rarity == Rarity::Common)
    {
        const double bd = rng.next();       // draw: budget for COMMON
        budget = (bd < 0.5) ? 1 : 0;
    }
    else
    {
        rng.next();                          // draw: consumed but discarded
        switch (rarity)
        {
        case Rarity::Uncommon:   budget = 2; break;
        case Rarity::Rare:       budget = 3; break;
        case Rarity::Exceptional: budget = 4; break;
        default:                 budget = 1; break;
        }
    }

    // Step 3: capRoll
    const double capRoll = rng.next();
    int cap;
    switch (complexity)
    {
    case Complexity::Simple:     cap = 0; break;
    case Complexity::Structured: cap = 2; break;
    case Complexity::Complex:    cap = 4; break;
    case Complexity::Free:       cap = static_cast<int>(std::floor(capRoll * 5.0)); break;
    default:                     cap = static_cast<int>(std::floor(capRoll * 5.0)); break;
    }

    // Step 4: Fisher-Yates shuffle of compatible modifier index list
    // Direction: i from (len-1) down to 1 (exactly as in JS)
    std::vector<int> cand = compatible_modifiers(m.family);
    const int clen = static_cast<int>(cand.size());
    for (int i = clen - 1; i > 0; --i)
    {
        const double jd = rng.next();                         // draw: swap index
        const int    j  = static_cast<int>(jd * (i + 1));
        std::swap(cand[i], cand[static_cast<std::size_t>(j)]);
    }

    // Step 5: apply modifiers up to min(cap, budget, cand.size())
    int n = std::min({cap, budget, clen});
    for (int ci : cand)
    {
        if (n <= 0) break;
        bool applied = false;
        switch (ci)
        {
        case 0: applied = mod_hotspot(m, rng);           break;
        case 1: applied = mod_companion(m, rng, N);      break;
        case 2: applied = mod_asymmetric(m, rng);        break;
        case 3: applied = mod_clumpy(m, rng);            break;
        case 4: applied = mod_central_cavity(m, rng, N); break;
        case 5: applied = mod_obscured(m, rng, N);       break;
        case 6: applied = mod_counterjet(m, rng);        break;
        case 7: applied = mod_precessing(m, rng);        break;
        default: break;
        }
        if (applied)
        {
            m.modifiers.push_back(kModifierNames[ci]);
            m.features.push_back(kFeatureLabels[ci]);
            --n;
        }
    }

    // Post-hoc reclassification (no draw)
    if (rarity == Rarity::Common && m.components.size() >= 5)
        m.rarity = Rarity::ComplexTier;
}

// ─────────────────────────────────────────────────────────────────────────────
// dispatch_recipe
// ─────────────────────────────────────────────────────────────────────────────

void dispatch_recipe(TargetModel& m, Rng& rng, std::uint32_t N, Family family)
{
    switch (family)
    {
    case Family::Binary:    recipe_binary(m, rng, N);      break;
    case Family::Star:      recipe_star(m, rng, N);        break;
    case Family::ProtoDisk: recipe_proto_disk(m, rng, N);  break;
    case Family::Nova:      recipe_nova(m, rng, N);        break;
    case Family::Agn:       recipe_agn(m, rng, N);         break;
    case Family::Compact:   recipe_compact(m, rng, N);     break;
    case Family::Planetary: recipe_planetary(m, rng, N);   break;
    case Family::PlanetRes: recipe_planet_res(m, rng, N);  break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public label functions (declared in target_families.hpp)
// ─────────────────────────────────────────────────────────────────────────────

std::string generate_designation(std::uint32_t seed)
{
    // JS: const h=seed>>>0,raMin=h%1440;
    //     const d2=Math.imul(h,2654435761)>>>0,dec=d2%5400;
    //     const sgn=((d2>>>16)&1)?"+":"−";
    //     "GW J"+padded(raMin/60|0,2)+padded(raMin%60,2)+sgn+...
    const std::uint32_t h     = seed;
    const std::uint32_t raMin = h % 1440u;
    const std::uint32_t d2    = h * 2654435761u; // Math.imul = modular uint32 multiply
    const std::uint32_t dec   = d2 % 5400u;
    const char          sgn   = ((d2 >> 16u) & 1u) ? '+' : '\u2212';

    auto pad2 = [](std::uint32_t v) -> std::string
    {
        std::string s = std::to_string(v);
        if (s.size() < 2) s = "0" + s;
        return s;
    };

    return std::string("GW J")
         + pad2(raMin / 60u) + pad2(raMin % 60u)
         + sgn
         + pad2(dec / 60u) + pad2(dec % 60u);
}

std::string family_label(Family f)
{
    // Italian labels — preserved verbatim from sandbox FAMILY_LABEL
    switch (f)
    {
    case Family::Binary:    return "SISTEMA BINARIO";
    case Family::Star:      return "STELLA SINGOLA";
    case Family::ProtoDisk: return "DISCO PROTOPL.";
    case Family::Nova:      return "TRANSIENTE";
    case Family::Agn:       return "AGN / RADIOGAL.";
    case Family::Compact:   return "OGGETTO COMPATTO";
    case Family::Planetary: return "SISTEMA PLANETARIO";
    case Family::PlanetRes: return "PIANETA RISOLTO";
    default:                return "SCONOSCIUTO";
    }
}

std::string subtype_label(const std::string& subtype)
{
    // Italian labels — preserved verbatim from sandbox SUBTYPE_LABEL
    if (subtype == "wide_binary")        return "BINARIA LARGA";
    if (subtype == "contact_binary")     return "BINARIA A CONTATTO";
    if (subtype == "supergiant")         return "SUPERGIGANTE";
    if (subtype == "oblate_star")        return "STELLA OBLATA";
    if (subtype == "brown_dwarf")        return "NANA BRUNA";
    if (subtype == "t_tauri")            return "T TAURI";
    if (subtype == "wolf_rayet")         return "WOLF-RAYET";
    if (subtype == "dying_supergiant")   return "SUPERGIGANTE MORENTE";
    if (subtype == "classic_disk")       return "DISCO INCLINATO";
    if (subtype == "transition_disk")    return "TRANSITION DISK";
    if (subtype == "multi_ring_disk")    return "ANELLI MULTIPLI";
    if (subtype == "nova_shell")         return "GUSCIO DI NOVA";
    if (subtype == "pulsar")             return "PULSAR";
    if (subtype == "core_jet")           return "NUCLEO+GETTO";
    if (subtype == "double_lobe")        return "DOPPIO LOBO (FR II)";
    if (subtype == "bh_crescent")        return "CRESCENTE EHT";
    if (subtype == "sculpted_disk")      return "DISCO SCOLPITO";
    if (subtype == "young_system")       return "SISTEMA GIOVANE";
    if (subtype == "planet_ocean")       return "OCEANICO";
    if (subtype == "planet_arid")        return "ARIDO";
    if (subtype == "planet_giant")       return "GIGANTE A BANDE";
    if (subtype == "planet_ice")         return "GHIACCIATO";
    return subtype;
}

} // namespace parallax::procedural
