# Sprint 10b — Interferometry & Aperture Synthesis

**Prerequisite:** Sprint 10a complete (ArrayInstrument, physical SNR, total-power imaging) ✅
**Oracle (conceptual reference):** `glasswing-sandbox-v1_7.html` — the interferometry pipeline
**Goal:** Real aperture synthesis. Earth-Moon baselines sampled from ephemerides, (u,v) coverage
built over an observation, gridding → dirty image, Högbom CLEAN reconstruction, closure phases.
Cross-validate the C++ against fixtures exported from the sandbox.
**Deliverable:** The Glasswing Array becomes a true interferometer. Point at a resolved procedural
target, run an aperture-synthesis observation, watch (u,v) coverage accumulate, reconstruct an
image via CLEAN, achieve angular resolution λ/B_max (micro-arcseconds with Earth-Moon baselines).

---

## The Sandbox as Conceptual Oracle

The `glasswing-sandbox-v1_7.html` implements the complete interferometry pipeline in JavaScript.
It is the **conceptual reference**: the C++ must replicate its mathematics faithfully (the math is
correct and validated), but the C++ MAY diverge in implementation details — data structures,
memory layout, optimizations, code organization — as long as the numbers match within tolerance.

**What must match (verified against exported fixtures):**
- (u,v) sample coordinates: relative 1e-9
- Complex visibilities: relative 1e-7
- Dirty image / dirty beam pixels: absolute 1e-6 of peak

**What may differ freely:**
- Class/struct design, file organization
- FFT library choice (reuse the 10a FFT)
- Performance optimizations
- Threading
- Rendering of the (u,v) plane and images (visual, not numeric)

**How to get fixtures:** open the sandbox in a browser, click "⭳ BATTERIA FIXTURE 10b" to export
`glasswing_fixture_battery_v1_1.json` (15 deterministic scenarios covering all target families and
regimes). Also export individual single-target datasets ("testset") as needed. The RNG is
`mulberry32` — replicate it exactly so noise/atmosphere seeds line up.

---

## Array Configuration (data-driven — mandatory)

The Tycho Base interferometric array must be **data-driven from the start** (CLAUDE.md
Section 9.1). Do NOT hardcode station count or layout.

**Y-shaped array (VLA-style):**
- Three arms at 120°, antennas distributed along each arm
- **Selectable site extent: 1 km / 10 km / 100 km** (the sandbox `scale` parameter,
  `ui.scale` = full extent; internally `scale/2` is the arm half-length used to place
  stations). Larger extent → longer baselines → finer angular resolution.
- Antenna count sufficient to populate the three arms at every scale
- The sandbox provides array-geometry generators (ring/grid/random and the site
  placement math in `buildStations`). Port the Y-arm generator following the same
  station-placement convention (lat/lon offsets from the site center on Moon or Earth).

**Sandbox Y preset (normative geometry):**
```
For arm in 0..2:  a = arm × 2.0944 − π/2        // three arms at 120° (2.0944 ≈ 2π/3)
  For i in 1..4:  r = (i/4)^1.7 × 0.85           // 4 antennas per arm, non-linear spacing
    station = { x: cos(a)×r, y: sin(a)×r }
Add central station { x:0, y:0 }                  // → 13 stations total
```
The normalized (x,y) in [-1,1] are then scaled by the site extent and converted to
lat/lon offsets from the site center (Moon: divide by R_MOON; Earth: by R_EARTH),
exactly as `buildStations` does. Antenna-per-arm count is configurable (4 is the
sandbox default → 13 stations); more antennas at larger scales is allowed.

**Configuration format:** an array config (JSON) defines: array geometry (Y),
antenna count, site extent, per-station aperture, and which spectral bands are
available. This same format will later support in-game upgrades (adding antennas,
extending the site, unlocking bands) — but the upgrade GAMEPLAY is a later sprint.
For 10b: load the array from config; no fixed-count assumptions in the pipeline code.

The Earth stations (La Palma, Mauna Kea, Paranal) remain their real fixed locations
for the mixed Earth-Moon interferometry mode.

---

## Physics & Pipeline (mirrors the sandbox)

### 1. Earth-Moon Ephemerides

Geocentric equatorial inertial frame. Station positions are functions of time.

**Constants (normative, from sandbox):**
```
OMEGA_E = 15.0 × π/180          rad/h   (Earth rotation)
OMEGA_M = (360/(27.321661×24)) × π/180  rad/h  (sidereal month)
D_MOON  = 384400e3 m            (Earth-Moon distance)
R_MOON  = 1737.4e3 m
R_EARTH = 6371e3 m
INC_MOON = 20 × π/180 rad       (orbital inclination, simplified)
TYCHO   = { lat: -43.3°, lon: -11.2° } in radians
moonPhase0 = 70 × π/180 rad     (initial orbital angle)
EL_MIN  = 10 × π/180 rad        (minimum elevation for visibility)
```

**Moon center position at time t (hours):**
```
a = moonPhase0 + OMEGA_M × t
moonCenter = [ D_MOON×cos(a),
               D_MOON×sin(a)×cos(INC_MOON),
               D_MOON×sin(a)×sin(INC_MOON) ]
```

**Station state (position P + local up vector) at time t:**

Earth station:
```
θ = lon + OMEGA_E × t
up = [ cos(lat)cos(θ), cos(lat)sin(θ), sin(lat) ]
P  = R_EARTH × up
```

Moon station (synchronous rotation — local frame follows orbital angle):
```
Cm = moonCenter(t);  Dm = |Cm|
e  = -Cm / Dm                        // points toward Earth
kv = [0, -sin(INC_MOON), cos(INC_MOON)]   // orbit normal
mv = kv × e                          // cross product
up = cos(lat)×(cos(lon)×e + sin(lon)×mv) + sin(lat)×kv
P  = Cm + R_MOON × up
```

**Occultation check** (does body at center C radius R block the source direction s3?):
```
d = C - P
proj = d · s3
if proj <= 0: not occulted (body behind station)
d2 = |d|² - proj²
occulted if d2 < R²
```
Earth stations can be occulted by the Moon; Moon stations by the Earth.

### 2. (u,v) Sampling

For a source at declination `dec`, source direction and uv-plane basis:
```
s3 = [ cos(dec), 0, sin(dec) ]         // source direction
eU = [ 0, -1, 0 ]                       // uv-plane basis vectors
eV = [ sin(dec), 0, -cos(dec) ]
```

For each pair of stations (i,j) at each sample time k:
```
1. Check visibility: station up · s3 >= sin(EL_MIN), and not occulted
2. Baseline vector: B = P_i - P_j
3. u = (B · eU) / λ
   v = (B · eV) / λ
4. Grid cell: GX = N/2 + u/du, GY = N/2 + v/du   where du = 1/thetaFov
5. If inside grid, sample the target's Fourier transform (bilinear) at (GX,GY)
   → true visibility (tVr, tVi)
```

**Time sampling:** if rotation enabled, K=48 sample times spread over the observation
duration (durH hours): `Hs[k] = ((k/(K-1)) - 0.5) × durH`. Cap total samples: if
`pairs × K > 8000`, reduce K. Without rotation, K=1 (snapshot).

### 3. Station Errors (atmosphere, gain, noise)

Applied to the true visibility to produce the measured visibility:

**Atmospheric phase** (Kolmogorov time series per station, `kolmSeries`):
```
For each station, generate a phase series with ~Kolmogorov spectrum:
  M=12 modes, amplitude_m = m^(-4/3), random phases
  ph[k] = Σ_m amplitude_m × sin(2π m k/K + phase_m)
  normalized to the requested RMS (turbulenceRms)
Applied as phase difference: dphi = ph_i[k] - ph_j[k]
```

**Gain errors** (per station): `gain = max(0.3, 1 + randn × 0.18)` when enabled.

**Thermal noise:** `noiseSig = flux/snr` (when snr > 0), added to Vr and Vi as Gaussian.

**Measured visibility:**
```
g = gain_i × gain_j
Vr = g × (tVr×cos(dphi) - tVi×sin(dphi)) + noise
Vi = g × (tVr×sin(dphi) + tVi×cos(dphi)) + noise
```

### 4. Instrument Modes

- `radio` — direct interferometry (complex visibility), Earth+Moon baselines
- `comb` — optical combiner, Earth-only baselines (coherence on ground)
- `hbt` — Hanbury Brown–Twiss intensity interferometry: amplitude only
  (`tVr = |V|, tVi = 0`), immune to atmospheric phase (dphi = 0)
- `epr` — sci-fi entanglement-based, Earth-Moon optical coherence

### 5. Gridding → Dirty Image + Dirty Beam

```
For each visibility point, grid it (and its conjugate at -u,-v):
  gRe[cell] += Vr;  gIm[cell] += Vi;  W[cell] += 1
  conjugate cell: gRe += Vr; gIm -= Vi; W += 1

If uniform weighting: divide each occupied cell by its weight W.

Dirty beam  = IFFT2(W)   (real part, fftshifted)
Dirty image = IFFT2(gRe + i·gIm)   (real part, fftshifted)

Normalize both by the beam peak (bp = beam[center]):
  beam /= bp;  dirty /= bp
```

Use the 10a FFT utilities. `shift2` for fftshift.

### 6. Högbom CLEAN

```
res = copy(dirty);  model = []
pk0 = max|res|
for it in 0..niter:
  find peak (pk, pi) in |res|
  if pk < 0.02 × pk0: break
  f = gain × res[pi]                    // loop gain, e.g. 0.1
  model.push(px, py, f)
  subtract f × shifted_beam from res
restore:
  rest = res + Σ model_component ⊛ Gaussian(fwhm)
  fwhm from the uv extent: fwhmPx ≈ 0.9/rmax/(thetaFov/N), clamped [2,24]
```

### 7. Closure Phases

For up to 3 station triangles (a,b,c), at each time k:
```
closure_observed = arg(V_ab) + arg(V_bc) - arg(V_ac)
closure_true     = arg(tV_ab) + arg(tV_bc) - arg(tV_ac)
(wrapped to [-π, π])
```
Closure phase is immune to station-based phase errors — a key diagnostic.

---

## Target Model (procedural families)

The sandbox generates procedural targets in 8 families, each with subtypes:

| Family | Label | Subtypes (examples) |
|--------|-------|---------------------|
| BINARY | Binary system | wide_binary, contact_binary |
| STAR | Single star | supergiant, oblate_star, brown_dwarf, t_tauri, wolf_rayet, dying_supergiant |
| PROTO_DISK | Protoplanetary disk | classic_disk, transition_disk, multi_ring_disk |
| NOVA | Transient | nova_shell, pulsar |
| AGN | AGN / radio galaxy | core_jet, double_lobe |
| COMPACT | Compact object | bh_crescent (EHT-style) |
| PLANETARY | Planetary system | sculpted_disk, young_system |
| PLANET_RES | Resolved planet | planet_ocean, planet_arid, planet_giant, planet_ice |

Each target renders to a sky image (grid N=128 for fixtures), from which the FFT
(target.Fre, target.Fim) is computed. The interferometer samples this FT at the (u,v)
points. This target model must be ported to C++ and integrated with the Universe/procedural
system from Sprint 07 (these ARE the procedural objects, revealed by observation).

**Note:** the sandbox target model is the concrete implementation of the procedural
generator described in CLAUDE.md Section 7c. Port the families faithfully; they become
the procedural objects that the Knowledge System reveals level by level.

---

## Architecture (C++)

```cpp
// src/interferometry/ephemeris.hpp
namespace parallax::interferometry {
    struct StationState { Vec3d position; Vec3d up; };
    StationState station_state(const Station& s, f64 time_hours);
    Vec3d moon_center_at(f64 time_hours);
    bool occulted_by(const Vec3d& P, const Vec3d& source_dir,
                     const Vec3d& body_center, f64 body_radius);
}

// src/interferometry/uv_sampling.hpp
struct Visibility { f64 u, v, Vr, Vi, tVr, tVi; u32 time_index; };
std::vector<Visibility> sample_uv(
    const std::vector<Station>& stations,
    const ObservationConfig& config,   // dec, lambda, duration, rotation, mode, weighting
    const TargetFT& target_ft,          // Fourier transform of the target sky
    const StationErrors& errors);       // turbulence, gain, noise + seed

// src/interferometry/imaging.hpp
struct DirtyImages { std::vector<f32> beam; std::vector<f32> dirty; u32 N; f64 du; };
DirtyImages make_images(const std::vector<Visibility>& pts, f64 du, u32 N, Weighting w);

// src/interferometry/clean.hpp
struct CleanResult { std::vector<f32> restored, residual; u32 ncomp, iters; f64 flux; };
CleanResult hogbom(const std::vector<f32>& dirty, const std::vector<f32>& beam,
                   u32 N, u32 niter, f64 gain, f64 fwhm_px);

// src/interferometry/closure.hpp
std::vector<ClosureTriangle> compute_closure_phases(
    const std::vector<Visibility>& pts, const std::vector<Station>& stations);
```

---

## Tasks

### Task 10b.1 — Ephemerides
Files: `src/interferometry/ephemeris.hpp/cpp`, `tests/test_ephemeris.cpp`
Implement moon_center_at, station_state (Earth + Moon), occulted_by.
Port constants exactly. Tests: verify station positions at t=0 and t=6h against
sandbox values; verify occultation geometry.

### Task 10b.2 — (u,v) Sampling
Files: `src/interferometry/uv_sampling.hpp/cpp`, plus Kolmogorov phase series + mulberry32 RNG.
Implement the full sampling loop: pair enumeration, time sampling, visibility/occultation
checks, baseline → (u,v), bilinear FT sampling, station errors (atmosphere/gain/noise),
instrument modes (radio/comb/hbt/epr).
Port mulberry32 exactly for seed compatibility with fixtures.

### Task 10b.3 — Gridding & Dirty Images
Files: `src/interferometry/imaging.hpp/cpp`
Implement make_images: gridding with conjugates, natural/uniform weighting,
FFT to beam and dirty image, beam-peak normalization. Reuse 10a FFT.
Assert: beam[center] normalizes to 1.

### Task 10b.4 — Högbom CLEAN
Files: `src/interferometry/clean.hpp/cpp`, `tests/test_clean.cpp`
Implement Högbom CLEAN with restore. fwhm from uv extent.
Test: a point source reconstructs to a point; flux is conserved reasonably.

### Task 10b.5 — Closure Phases
Files: `src/interferometry/closure.hpp/cpp`
Compute closure phases for up to 3 triangles. Verify closure phase is immune to
station phase errors (inject phase errors, closure stays near true).

### Task 10b.6 — Target Model Port
Files: `src/procedural/target_families.hpp/cpp` (integrate with Sprint 07 procedural)
Port the 8 families + subtypes. Each renders a sky grid + computes its FT.
This is the concrete procedural generator (CLAUDE.md 7c). Deterministic from seed.

### Task 10b.7 — Fixture Cross-Validation
Files: `tests/test_interferometry_fixtures.cpp`, `data/fixtures/glasswing_fixture_battery_v1_1.json`
Load the fixture battery. For each of the 15 fixtures, run the C++ pipeline with the
same inputs and assert:
- (u,v) coordinates: relative 1e-9
- visibilities: relative 1e-7
- dirty image / beam: absolute 1e-6 of peak
This is the acceptance gate for the whole sprint.

### Task 10b.8 — Interferometry Tab / Imaging Tab Integration
Files: `src/ui/tabs/imaging_tab.cpp` (extend), or new interferometry view
- (u,v) coverage plot (accumulates during observation)
- Dirty image, CLEAN reconstruction, residuals views
- Controls: instrument mode, weighting, rotation, duration, CLEAN iterations/gain,
  turbulence, SNR, gain errors
- Live observation: (u,v) track accumulates in real time as the array observes
- Angular resolution readout: λ/B_max (show micro-arcsecond scale)
- The "OWL can't beat this" teaching moment: compare single-dish resolution to
  interferometric resolution for the same target

### Task 10b.9 — Integration
- Wire interferometry into ArrayInstrument (the array now has an interferometric mode)
- Sessions can be interferometric observations
- Knowledge unlocks from resolved structure (L5 Resolved via interferometry)
- Save/load observation configuration

---

## Definition of Done

- [ ] Ephemerides match sandbox station positions (Earth + Moon) over time
- [ ] (u,v) sampling reproduces sandbox coordinates within 1e-9
- [ ] Visibilities match within 1e-7 (including atmosphere/gain/noise with same seeds)
- [ ] Dirty image + beam match within 1e-6 of peak
- [ ] Högbom CLEAN reconstructs point sources and extended structure
- [ ] Closure phases immune to station phase errors
- [ ] All 8 target families port and render deterministically
- [ ] **All 15 fixtures in the battery pass** (the acceptance gate)
- [ ] (u,v) coverage accumulates live during an observation
- [ ] Angular resolution readout shows λ/B_max (micro-arcsec with Earth-Moon)
- [ ] Instrument modes work (radio/comb/hbt/epr)
- [ ] Natural + uniform weighting
- [ ] Knowledge L5 (Resolved) unlocks from interferometric imaging
- [ ] No regressions in 10a; ≥ 60fps; no Vulkan validation errors

---

## Common Pitfalls

```
❌ DO NOT reimplement the RNG differently — mulberry32 must match bit-for-bit for fixtures
❌ DO NOT store angles in arcsec internally — radians everywhere, convert at display
❌ DO NOT skip the conjugate points in gridding — Hermitian symmetry is required
❌ DO NOT forget beam-peak normalization
❌ DO NOT apply atmospheric phase in HBT mode (intensity interferometry is phase-immune)
❌ DO NOT diverge from sandbox math in ways that break fixtures — details free, numbers fixed
✅ DO reuse the 10a FFT — no new FFT
✅ DO port constants exactly from the sandbox
✅ DO validate against fixtures early and often
✅ DO keep the target model as the concrete procedural generator (integrates Sprint 07)
✅ DO surface the interferometry-beats-single-dish teaching moment
```

---

## Note on the Optical Imaging Trio

The `Parallax_Optical_Imaging_Trio_spec.md` (rev.2) defines a SEPARATE sprint slot
AFTER 10b: the three single-dish telescopes (GW-UWF 0.28m, GW-NF 8m, GW-OWL 100m)
with PSF via MTF product. That is NOT part of 10b — it is its own sprint (call it 10c
or fold into Sprint 11). Do not implement the trio here. 10b is interferometry only.

The pedagogical bridge: the trio tops out at ~1 mas (OWL at f/60), which the
interferometer beats by orders of magnitude via Earth-Moon baselines. That comparison
is the reason aperture synthesis exists in Parallax — surface it in both sprints.
