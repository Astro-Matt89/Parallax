#pragma once

/// @file target_families.hpp
/// @brief Procedural target-model generator — Sprint 10b Task 10b.6.
///
/// Ports the eight interferometric target families from the Glasswing sandbox
/// oracle (tools/glasswing-sandbox-v1_7.4.html, section "MODELLO TARGET COMPOSITIVO").
/// Numeric results at N=128 are bit-identical with the JS oracle.
///
/// ## Structural divergences from the sandbox (permitted; numbers unchanged)
/// - Global `N` and `RENDER_NU` are threaded as explicit parameters instead of
///   being mutable globals.  Default N=128 per the fixture contract.
/// - `lambda_m` and `epoch_days` ARE passed to rendering (evaluateSpectralFlux +
///   applyTemporal are called).  The sandbox v0.3 comment says "not yet used",
///   but the actual v1.7.4 `renderTargetAt` calls both — this port matches that
///   real behaviour.
/// - Italian label strings are preserved verbatim (they are data, not UI copy).
///
/// ## RNG draw-order contract (BINDING — changing breaks fixtures)
/// See SPECIFICA_10b_glasswing.md §2.  Draw order per `generate_target_model`:
///   1. Optional family draw — consumed only when `opts.forced_family` is nullopt.
///   2. Recipe body — all draws internal to `TargetRecipes[family]`.
///   3. Rarity roll (always one draw).
///   4. Budget draw (always one draw, even for COMMON).
///   5. Complexity capRoll (one draw, consumed even if complexity is string-keyed).
///   6. Fisher-Yates shuffle of compatible-modifier list (one draw per swap step).
///   7. Internal draws of each applied modifier.
///
/// ## Sprint 07 integration seam
/// `target_from_object_seed` maps a 64-bit universe object ID to a TargetModel.
/// The lower 32 bits of the object ID are used as the model seed.  Full wiring
/// to the Knowledge System is deferred (see implementation comment in
/// target_families.cpp).
///
/// @note RNG reuse: uses `parallax::interferometry::Mulberry32` (already ported
/// and tested).  NOT duplicated.
///
/// @note FFT output: uses `parallax::interferometry::TargetFT` so that
/// `sample_uv` can consume the result directly.

// ──────────────────────────────────────────────────────────────────────────────
// Project headers
// ──────────────────────────────────────────────────────────────────────────────
#include "interferometry/uv_sampling.hpp"   // TargetFT
#include "interferometry/mulberry32.hpp"    // Mulberry32 (bit-exact RNG)

// ──────────────────────────────────────────────────────────────────────────────
// Standard library
// ──────────────────────────────────────────────────────────────────────────────
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace parallax::procedural
{

// ═══════════════════════════════════════════════════════════════════════════════
// Enumerations
// ═══════════════════════════════════════════════════════════════════════════════

/// The eight procedural target families (mirror of JS `FAMILIES` array order).
enum class Family : std::uint8_t
{
    Binary     = 0,
    Star       = 1,
    ProtoDisk  = 2,
    Nova       = 3,
    Agn        = 4,
    Compact    = 5,
    Planetary  = 6,
    PlanetRes  = 7,
};
constexpr std::uint32_t kFamilyCount = 8u;

/// Complexity setting (maps to `opts.complexity` in the sandbox).
enum class Complexity : std::uint8_t
{
    Simple     = 0,   ///< No modifiers applied.
    Structured = 1,   ///< ≤ 2 modifiers (default).
    Complex    = 2,   ///< ≤ 4 modifiers.
    Free       = 3,   ///< Floor(capRoll * 5) modifiers.
};

/// Rarity tiers (JS `RARITY_BUDGET` keys).
enum class Rarity : std::uint8_t
{
    Common      = 0,
    ComplexTier = 1,  ///< "COMPLEX" — COMMON target with ≥ 5 components.
    Uncommon    = 2,
    Rare        = 3,
    Exceptional = 4,
};

/// Temporal evolution model.
enum class TemporalModel : std::uint8_t
{
    Static,
    Orbit,
    Rotation,
    Expansion,
    ProperMotion,
    MultiOrbit,
    PlanetRotation,
};

/// Morphological primitive type.
enum class PrimitiveType : std::uint8_t
{
    Point,
    Gaussian,
    Disk,
    Ring,
    Jet,
    PlanetSurface,
    Absorption,
};

/// Spectral model tag per component.
enum class SpectralModel : std::uint8_t
{
    Stellar,
    ThermalDust,
    Synchrotron,
    FreeFree,
    Maser,
};

// ═══════════════════════════════════════════════════════════════════════════════
// Component sub-structures
// ═══════════════════════════════════════════════════════════════════════════════

/// Azimuthal harmonic term for Ring primitive: contribution = A * cos(k*phi - ph).
struct HarmonicTerm
{
    int    k  {0};
    double A  {0.0};
    double ph {0.0};
};

/// One knot in a Jet primitive (baked position / amplitude / size).
struct JetKnot
{
    double t    {0.0};  ///< Normalised position along jet [0,1].
    double dAng {0.0};  ///< Angular perturbation (rad).
    double amp  {0.0};  ///< Flux amplitude scale.
    double sig  {0.0};  ///< Gaussian sigma (pixels).
};

/// Keplerian orbit description for planetary/WR components.
struct Orbit
{
    double aPx        {0.0};   ///< Semi-major axis in pixels.
    double periodYears{0.0};
    double phase0     {0.0};   ///< Initial orbital phase (rad).
    double cosI       {1.0};   ///< cos(inclination).
    double pa         {0.0};   ///< Position angle of orbit axis (rad).
};

// ═══════════════════════════════════════════════════════════════════════════════
// Component
// ═══════════════════════════════════════════════════════════════════════════════

/// One morphological component.  All fields are stored flat (no variant) to
/// avoid per-access overhead and keep the porting straightforward.
struct Component
{
    // --- Identity ---
    std::string    id;
    PrimitiveType  type         {PrimitiveType::Point};
    SpectralModel  spectral_model {SpectralModel::Stellar};

    // --- Position (pixels, centre = N/2) ---
    double x {0.0};
    double y {0.0};

    // --- Flux ---
    double flux                 {0.0};
    double flux_ref             {0.0};
    double reference_freq_hz    {230e9};

    // --- Spectral parameters (optional per type) ---
    double alpha         {-0.7};    ///< Synchrotron spectral index.
    double size_index    {0.0};     ///< Synchrotron size scaling index.
    double line_freq_hz  {0.0};     ///< Maser line frequency (Hz).
    double maser_amp     {0.0};     ///< Maser amplitude scale.

    // --- Point / Gaussian ---
    double sigma   {1.3};          ///< Isotropic sigma (Point uses this).
    double sigma_x {0.0};          ///< Rotated Gaussian semi-axis x.
    double sigma_y {0.0};          ///< Rotated Gaussian semi-axis y.
    double angle   {0.0};          ///< Rotation angle (rad).

    // --- Disk ---
    double radius        {0.0};
    double ellipticity   {0.0};
    double limb_darkening{0.0};

    // --- Ring ---
    double width         {0.0};
    std::vector<HarmonicTerm> harm;

    // --- Jet ---
    double length          {0.0};
    double curvature       {0.0};
    double counter_jet_ratio{0.0};
    std::vector<JetKnot>  knots;

    // --- PlanetSurface ---
    std::string kind;          ///< "planet_ocean" / "planet_arid" / …
    int         nz    {0};     ///< Integer noise seed.
    double rot_phase  {0.0};
    double cloud_phase{0.0};
    double phase_angle{0.0};
    double sea_level  {0.0};
    double cap_lat    {0.0};
    double cloud_amt  {0.0};

    // --- Absorption ---
    std::string shape;         ///< "disk" / "gaussian" / "band".
    double depth {0.0};

    // --- Temporal rotation flag (components that co-rotate with the star) ---
    bool rot {false};

    // --- Orbital parameters (planetary systems, Wolf-Rayet spirals) ---
    std::optional<Orbit> orbit;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Temporal sub-struct
// ═══════════════════════════════════════════════════════════════════════════════

struct TemporalData
{
    TemporalModel model           {TemporalModel::Static};
    double        phase0          {0.0};   ///< orbit / rotation
    double        period_years    {0.0};   ///< orbit / rotation / planet_rotation
    double        outer_period_years{0.0}; ///< multi_orbit
    double        knot_speed_c    {0.0};   ///< proper_motion (units of c)
    double        rate_px_per_year{0.0};   ///< expansion
};

// ═══════════════════════════════════════════════════════════════════════════════
// TargetModel
// ═══════════════════════════════════════════════════════════════════════════════

/// Complete description of one procedural target.
/// Produced by `generate_target_model`; consumed by `render_target_at`.
struct TargetModel
{
    // --- Identity ---
    std::uint32_t seed        {0};
    std::string   designation;        ///< "GW Jhhmm±ddmm" format.
    Family        family      {Family::Binary};
    int           fam_idx     {0};
    std::string   subtype;            ///< Italian subtype key (verbatim from JS).

    // --- Rarity ---
    Rarity rarity {Rarity::Common};

    // --- Geometry ---
    double theta_obj {0.0};   ///< Angular diameter of object (radians).
    double fov_mul   {2.5};   ///< FOV multiplier (fovMul in JS).

    // --- Components & modifiers ---
    std::vector<Component>   components;
    std::vector<std::string> modifiers;   ///< Applied modifier names.

    // --- Temporal ---
    TemporalData temporal;

    // --- Physical parameters (free-form strings for display) ---
    std::string dist_label;   ///< metadata.dist
    std::string phys_label;   ///< metadata.phys
    std::string extra_label;  ///< metadata.extra

    // --- Metadata features list ---
    std::vector<std::string> features;

    // --- Summary ---
    std::string summary;      ///< "FAMILY_LABEL · SUBTYPE_LABEL"

    // --- Physical numbers (keyed by family) ---
    // Rather than a large per-family struct, physical numbers are stored as
    // a flat map of named doubles for flexibility and portability.
    // Common keys include: separation_au, distance_pc, flux_ratio, etc.
    // Consumers needing type-safe physics should pattern-match on `family`.
    struct PhysicalMap
    {
        double separation_au         {0.0};
        double distance_pc           {0.0};
        double distance_kpc          {0.0};
        double distance_mpc          {0.0};
        double flux_ratio            {0.0};
        double total_mass_solar      {0.0};
        double primary_radius_solar  {0.0};
        double mass_ratio            {0.0};
        double radius_solar          {0.0};
        double limb_darkening        {0.0};
        double oblateness            {0.0};
        double radius_jupiter        {0.0};
        double temp_k                {0.0};
        double rotation_hours        {0.0};
        double inner_disk_au         {0.0};
        double inclination_deg       {0.0};
        double jet_length_pc         {0.0};
        double spiral_radius_au      {0.0};
        double period_years          {0.0};
        double shell_age_years       {0.0};
        double disk_radius_au        {0.0};
        double position_angle_deg    {0.0};
        double cavity_au             {0.0};
        int    ring_count            {0};
        double nebula_radius_pc      {0.0};
        double period_ms             {0.0};
        double dm_pc_cm3             {0.0};
        double shell_radius_au       {0.0};
        double expansion_kms         {0.0};
        double age_years             {0.0};
        double core_spectral_index   {0.0};
        double jet_spectral_index    {0.0};
        double jet_length_kpc        {0.0};  // same as above but kpc units
        double knot_count            {0.0};
        double jet_pa_deg            {0.0};
        double lobe_span_kpc         {0.0};
        double lobe_asymmetry        {0.0};
        double ring_diameter_uas     {0.0};
        double mass_billion_solar    {0.0};
        double doppler_asymmetry     {0.0};
        double star_mass_solar       {0.0};
        double outer_ring_au         {0.0};
        int    planet_count          {0};
        double outermost_au          {0.0};
        double radius_earth          {0.0};
        std::string surface_type;
    } physical;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Options
// ═══════════════════════════════════════════════════════════════════════════════

/// Options for `generate_target_model`.
struct TargetOptions
{
    /// Force a specific family.  nullopt = random (consumes one RNG draw).
    std::optional<Family> forced_family;

    /// Modifier complexity cap.  Default: Structured (≤ 2 modifiers).
    Complexity complexity {Complexity::Structured};
};

// ═══════════════════════════════════════════════════════════════════════════════
// Constants
// ═══════════════════════════════════════════════════════════════════════════════

/// Maximum number of components per model (JS MAX_COMPONENTS = 24).
constexpr std::uint32_t kMaxComponents = 24u;

/// Speed of light (m/s) — matches JS C_LIGHT = 299792458.
constexpr double kCLight = 299792458.0;

/// Reference rendering frequency (Hz) — JS RENDER_NU default = 230e9 (1.3mm).
/// Threaded as parameter in C++; this constant is the default.
constexpr double kDefaultRenderNu = 230e9;

// ═══════════════════════════════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════════════════════════════

/// @brief Generate a procedural target model from a 32-bit seed.
///
/// Draw-order contract: see file-level documentation and
/// SPECIFICA_10b_glasswing.md §2.
///
/// @param seed     32-bit seed (maps to a deterministic model).
/// @param opts     Options: forced family, complexity.
/// @returns Fully populated TargetModel.
[[nodiscard]] TargetModel generate_target_model(std::uint32_t seed,
                                                 const TargetOptions& opts = {});

/// @brief Render a sky image from a TargetModel.
///
/// @param model      Target model (from `generate_target_model`).
/// @param lambda_m   Observing wavelength (metres).  Default: 1.3mm.
/// @param epoch_days Epoch offset (days, for temporal evolution).  Default: 0.
/// @param N          Grid side length (must be power of 2).  Default: 128.
/// @returns Flat N×N sky image in row-major order (index = y*N + x), non-negative.
[[nodiscard]] std::vector<double> render_target_at(const TargetModel& model,
                                                    double             lambda_m   = kCLight / kDefaultRenderNu,
                                                    double             epoch_days = 0.0,
                                                    std::uint32_t      N          = 128u);

/// @brief Compute the 2-D FFT of a rendered sky image, ready for uv-sampling.
///
/// Replicates the sandbox `computeTargetFFT` convention exactly:
///   1. Sum sky for total flux.
///   2. Copy sky → re; zero im.
///   3. shift2(re).
///   4. fft2(re, im, N).
///   5. shift2(re); shift2(im).
///
/// Uses `parallax::core::fft2` and `parallax::core::shift2`.
///
/// @param sky   N×N sky image from `render_target_at`.
/// @param N     Grid side length (must match the sky vector).
/// @returns `TargetFT{Fre, Fim, N}` compatible with `sample_uv`.
[[nodiscard]] interferometry::TargetFT compute_target_fft(const std::vector<double>& sky,
                                                           std::uint32_t              N = 128u);

/// @brief Sprint 07 integration seam: generate a TargetModel for a procedural
///        celestial object identified by its 64-bit universe object seed.
///
/// The lower 32 bits of `object_seed` are used as the TargetModel seed.
/// This provides a deterministic, per-object interferometric model without
/// requiring the observation subsystem to manage seeds separately.
///
/// Full wiring to the Knowledge System (level-by-level reveal of the family,
/// subtype, and physical parameters) is deferred to a follow-up task.
///
/// @param object_seed  64-bit universe object ID (from ProceduralProvider).
/// @param opts         Standard generation options.
[[nodiscard]] TargetModel target_from_object_seed(std::uint64_t        object_seed,
                                                   const TargetOptions& opts = {});

// ═══════════════════════════════════════════════════════════════════════════════
// Noise functions (declared here; implementation in target_primitives.cpp)
// These are exposed for testing.
// ═══════════════════════════════════════════════════════════════════════════════

/// @brief Value hash — deterministic pseudo-random in [0,1).
/// Bit-faithful port of the JS oracle function of the same name.
[[nodiscard]] double vhash(std::int32_t ix, std::int32_t iy, std::int32_t s) noexcept;

/// @brief Value noise — smooth interpolation of vhash on a regular lattice.
[[nodiscard]] double vnoise(double x, double y, std::int32_t s) noexcept;

/// @brief Fractal Brownian motion (3 octaves).
[[nodiscard]] double fbm2(double x, double y, std::int32_t s) noexcept;

/// @brief Designation formatter: "GW Jhhmm[±]ddmm".
[[nodiscard]] std::string generate_designation(std::uint32_t seed);

/// @brief Italian family label (verbatim from sandbox FAMILY_LABEL).
[[nodiscard]] std::string family_label(Family f);

/// @brief Italian subtype label (verbatim from sandbox SUBTYPE_LABEL).
[[nodiscard]] std::string subtype_label(const std::string& subtype);

} // namespace parallax::procedural
