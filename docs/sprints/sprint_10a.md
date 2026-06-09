# Sprint 10a — Array Instrument & Physical Imaging

**Prerequisite:** Sprint 09 complete (UI shell with tabs, lunar base setting)
**Goal:** Replace the mock instrument with a real multispectral array instrument. Physical SNR model. Live schematic preview + integrated realistic image. FITS + PNG export.
**Deliverable:** The player configures a multi-station array (Moon + Earth), points it at a target, selects spectral bands, runs an integration in the Imaging tab, watches the image form with realistic noise/PSF, and saves multispectral results. SNR is physically computed. NO interferometry yet (total-power mode) — that's Sprint 10b.

---

## Scope Discipline

Sprint 10a builds the array as a **total-power instrument**: the stations act as
one big light collector. Combined collecting area gives SNR, but NOT extra angular
resolution. Angular resolution in 10a is limited by the largest single aperture
(diffraction limit of one station).

**Sprint 10b** will add aperture synthesis (interferometry): using the baselines
between stations to achieve angular resolution of λ/B_max. That's where the
microarcsecond resolution and surface-resolving power comes from.

This split is physically correct — real arrays operate in both modes.
Do NOT attempt interferometric reconstruction in 10a. Keep it simple:
combined aperture → SNR → image at single-aperture resolution.

---

## Architecture

### The Instrument Model

Replace `MockInstrument` (Sprint 08) with a real `Instrument` hierarchy.

```cpp
namespace parallax::instruments
{
    // A single collecting element of the array
    struct Station
    {
        std::string name;              // "Tycho Primary", "La Palma Station"
        astro::ParentBody body;        // Moon, Earth
        f64 latitude_rad;
        f64 longitude_rad;
        f64 elevation_m;

        f32 aperture_diameter_m;       // Collecting aperture
        f32 efficiency;                // Optical+quantum efficiency (0..1)
        bool has_atmosphere;           // Earth stations suffer atmosphere
        bool is_active;                // Player can toggle stations on/off
    };

    // A spectral band the instrument can observe in
    struct SpectralBand
    {
        std::string name;              // "Visible", "Near-IR", "Radio-K"
        f64 center_wavelength_nm;
        f64 bandwidth_nm;
        bool is_unlocked;              // Progression: unlock more bands
    };

    // The player's array instrument
    class ArrayInstrument
    {
    public:
        ArrayInstrument(u64 id, const std::string& name);

        // Station management
        void add_station(const Station& station);
        void set_station_active(u32 index, bool active);
        [[nodiscard]] std::span<const Station> get_stations() const;
        [[nodiscard]] u32 get_active_station_count() const;

        // Combined collecting area (sum of active stations, total-power mode)
        [[nodiscard]] f64 get_total_collecting_area_m2() const;

        // Angular resolution (10a: diffraction limit of largest single aperture)
        // (10b will override this with baseline-based resolution)
        [[nodiscard]] f64 get_angular_resolution_arcsec(f64 wavelength_nm) const;

        // Spectral bands
        [[nodiscard]] std::span<const SpectralBand> get_bands() const;
        void set_band_active(u32 index, bool active);
        [[nodiscard]] std::vector<u32> get_active_bands() const;

        // Field of view (depends on detector + focal config — simplified)
        [[nodiscard]] f64 get_fov_arcsec() const;

        [[nodiscard]] u64 get_id() const;
        [[nodiscard]] const std::string& get_name() const;

    private:
        u64 m_id;
        std::string m_name;
        std::vector<Station> m_stations;
        std::vector<SpectralBand> m_bands;
        f64 m_fov_arcsec = 60.0;       // Default 1 arcmin FOV
    };
}
```

**Default array configuration (game start):**

```
"Glasswing Array" (sci-fi name, EHT-inspired)
Stations:
  1. Tycho Primary    — Moon, aperture 12m, no atmosphere, active
  2. La Palma         — Earth, aperture 8m, atmosphere, active
  3. Mauna Kea        — Earth, aperture 10m, atmosphere, active
  4. Paranal          — Earth, aperture 8m, atmosphere, active

Bands (unlocked at start):
  1. Visible          — 550nm, bandwidth 200nm, unlocked
  2. Near-IR          — 1600nm, bandwidth 400nm, unlocked
Bands (locked, future progression):
  3. Mid-IR           — 10000nm, locked
  4. Radio-K          — 1.3mm, locked (the EHT band!)
  5. Submm            — 850um, locked
```

### Physical SNR Model

This replaces the flat mock formula (5.0/hour). Real radiometry.

```cpp
namespace parallax::instruments
{
    struct SNRParameters
    {
        f64 target_flux_jy;            // Target flux density (Jansky) or mag-derived
        f64 collecting_area_m2;        // Total active aperture area
        f64 efficiency;                // System efficiency
        f64 bandwidth_hz;              // Spectral bandwidth
        f64 integration_time_s;        // Exposure time
        f64 system_temperature_k;      // Noise temperature (atmosphere + instrument)
        f64 sky_background;            // Background flux
        u32 num_stations;              // For noise averaging
    };

    class SNRCalculator
    {
    public:
        // Radiometer equation (simplified for game):
        //   SNR = signal / noise
        //   signal ∝ flux × area × efficiency × bandwidth × time
        //   noise ∝ sqrt(system_temp² × bandwidth × time) / sqrt(N_stations)
        [[nodiscard]] static f64 compute_snr(const SNRParameters& params);

        // Convert magnitude to flux density (Jansky)
        // Using band-appropriate zero points
        [[nodiscard]] static f64 magnitude_to_flux_jy(
            f64 magnitude, f64 wavelength_nm);

        // Time needed to reach a target SNR
        [[nodiscard]] static f64 time_to_reach_snr(
            f64 target_snr, const SNRParameters& params);
    };
}
```

**The radiometer equation (game-simplified):**

```
Signal rate (photons/sec or flux):
  S = flux_target × collecting_area × efficiency × bandwidth

Noise (radiometer equation):
  N = sqrt(S × t + background × t + (read_noise² × n_reads)) / sqrt(N_stations)
  (Poisson statistics on signal + background, plus read noise)

SNR after integration time t:
  SNR = (S × t) / N
      = S × t / sqrt((S + B) × t + read_noise_term)

For long integrations dominated by source + background:
  SNR ≈ sqrt(S × t)  (Poisson-limited)
  → To double SNR, integrate 4× longer. This is the gameplay tension.

Key physical truths the player learns:
- Brighter target → faster SNR
- Bigger combined aperture → faster SNR
- Wider bandwidth → faster SNR (but loses spectral resolution)
- More stations → less noise (averaging)
- Earth stations with atmosphere → higher system temperature → more noise
- Faint targets → painfully long integrations
```

**Magnitude → flux:**
```
For visible band, using Vega zero-point:
  flux_jy = 3631 × 10^(-0.4 × magnitude)   [Jansky, AB system approx]

For each band, use the appropriate zero point.
Fainter magnitude = exponentially less flux = much longer integration.
A mag 20 procedural galaxy needs enormously more time than a mag 5 star.
```

### Real-time Image Formation

The Imaging tab shows the image forming during integration.

**Two visual layers:**

1. **Live schematic preview** (always shown):
   - Shows the target field as known from skychart data
   - Overlays: FOV box, target marker, integration progress, current SNR
   - Shows which objects are in the field (from Universe query at this resolution)

2. **Integrated image** (builds up over integration time):
   - Starts as noise
   - Signal accumulates: objects emerge from noise as SNR grows
   - Realistic rendering: PSF (Airy disk), Poisson noise, read noise, background
   - Multispectral: one image layer per active band
   - At low SNR: barely visible smudges
   - At high SNR: crisp point sources / resolved structure (within single-aperture resolution)

**Image formation model (per pixel, per band):**
```
For each object in the field of view:
  1. Project to detector pixel coordinates
  2. Compute flux in this band (from object properties + band)
  3. Spread flux via PSF (Airy disk for the aperture, at this wavelength)
  4. Accumulate into the image buffer

Then per pixel:
  signal = accumulated_flux × integration_time × efficiency
  add Poisson noise: actual = poisson_sample(signal + background)
  add Gaussian read noise
  convert to ADU (analog-to-digital units)

The image buffer is float (electrons), displayed with a stretch (log/asinh)
for visualization.
```

**Angular resolution in 10a:**
```
θ = 1.22 × λ / D_largest_single_aperture   [diffraction limit]

For a 12m aperture at 550nm:
  θ = 1.22 × 550e-9 / 12 ≈ 0.0115 arcsec ≈ 11.5 mas

This is the PSF width. Objects closer than this blur together.
(Sprint 10b will dramatically improve this via baselines.)
```

### Multispectral Output

The instrument observes in multiple bands simultaneously (or sequentially —
simplify to "simultaneously" for game purposes).

Each band produces its own image layer. The Imaging tab can:
- Show individual band images
- Show a false-color composite (e.g., visible=green, near-IR=red)
- Allow the player to inspect each band

### FITS + PNG Export

```cpp
namespace parallax::imaging
{
    class ImageExporter
    {
    public:
        // Export multispectral data as a multi-extension FITS file
        // (one image HDU per band, with WCS and metadata headers)
        static bool export_fits(
            const MultispectralImage& image,
            const std::filesystem::path& path,
            const ObservationMetadata& metadata);

        // Export a single band or composite as PNG (for sharing)
        static bool export_png(
            const Image& image,
            const std::filesystem::path& path,
            StretchMode stretch);
    };
}
```

FITS: use cfitsio (add to vcpkg) or write a minimal FITS writer.
Metadata to include: target, RA/Dec, JD, integration time, bands, SNR,
instrument name, station list.

PNG: use stb_image_write (add to vcpkg). Apply stretch (linear/log/asinh)
before writing 8-bit or 16-bit PNG.

---

## Tasks

### Task 10a.1 — Instruments: Core Types

Replace mock instrument types with the real array model.

**Files:**
- `src/instruments/station.hpp`
- `src/instruments/spectral_band.hpp`
- `src/instruments/array_instrument.hpp`
- `src/instruments/array_instrument.cpp`

Implement Station, SpectralBand, ArrayInstrument per the architecture.
Include the default "Glasswing Array" configuration as a factory function:
`ArrayInstrument::create_default()`.

Keep MockInstrument for now (don't delete until Task 10a.8 integration).

**Acceptance:** ArrayInstrument compiles. Default config has 4 stations, 2 active bands.

---

### Task 10a.2 — Instruments: Physical SNR Model

The radiometer-equation SNR calculator.

**Files:**
- `src/instruments/snr_calculator.hpp`
- `src/instruments/snr_calculator.cpp`
- `tests/test_snr_calculator.cpp`

Implement SNRCalculator per the architecture.
- compute_snr() using the radiometer equation
- magnitude_to_flux_jy() with band-appropriate zero points
- time_to_reach_snr() inverse calculation

Tests:
- Brighter target reaches a given SNR faster than fainter
- Doubling integration time increases SNR by sqrt(2) in Poisson regime
- More active stations → higher SNR (noise averaging)
- Larger combined aperture → higher SNR
- Verify magnitude_to_flux: mag 0 ≈ 3631 Jy (visible), each 5 mag = 100× fainter

**Acceptance:** SNR scales physically correctly. Tests pass.

---

### Task 10a.3 — Imaging: Image Buffers and PSF

Core image data structures and PSF model.

**Files:**
- `src/imaging/image.hpp`            (single-band float image)
- `src/imaging/multispectral_image.hpp`
- `src/imaging/psf.hpp`
- `src/imaging/psf.cpp`

Image: 2D float buffer (electrons/flux), width × height, plus metadata.
MultispectralImage: collection of Image, one per band.

PSF (Point Spread Function):
- Airy disk model: I(θ) = [2 J₁(x)/x]² where x = π D sinθ / λ
- Simplified: a Gaussian approximation of the Airy disk is acceptable
  with FWHM = 1.028 λ/D
- Function to render a point source at sub-pixel position into the image
  with the PSF spread

**Acceptance:** Can create image buffers. PSF renders a point source as a
realistic blob of the correct angular size.

---

### Task 10a.4 — Imaging: Image Formation Engine

Synthesize a realistic image from the objects in the field.

**Files:**
- `src/imaging/image_formation.hpp`
- `src/imaging/image_formation.cpp`

The engine takes:
- A target pointing (RA/Dec center)
- FOV and detector resolution
- Active spectral bands
- The ArrayInstrument (for aperture, resolution, efficiency)
- Integration time so far
- The Universe (to query objects in the field)

It produces a MultispectralImage:
```
For each band:
  Create empty float image
  Query Universe for objects in the FOV (at instrument resolution)
  For each object:
    Compute flux in this band (object magnitude/properties + band)
    Project to detector pixel coordinates
    Render via PSF at the band's diffraction limit
    Scale by integration time × efficiency
  Add sky background (scaled by time)
  Add Poisson noise on (signal + background)
  Add Gaussian read noise
```

The formation is incremental: as integration_time grows, signal accumulates
and SNR improves. Noise is recomputed for the current total exposure.

For sub-universe objects (resolved galaxies, etc.): in 10a, objects below the
single-aperture resolution are rendered as unresolved point sources or simple
blobs. True resolution comes in 10b.

**Acceptance:**
- Bright target produces a clear image quickly
- Faint target is buried in noise at short integration, emerges over time
- Multiple objects in field all rendered
- Multispectral: each band shows appropriate flux

---

### Task 10a.5 — Imaging: Export (FITS + PNG)

Save images to disk.

**Files:**
- `src/imaging/image_exporter.hpp`
- `src/imaging/image_exporter.cpp`

Add to vcpkg.json: `cfitsio` (FITS), `stb` (PNG writing via stb_image_write).

FITS export:
- Multi-extension FITS (one image HDU per band)
- Primary HDU with metadata headers (OBJECT, RA, DEC, DATE-OBS, EXPTIME,
  INSTRUME, plus custom headers for bands and SNR)
- WCS keywords (CRVAL, CRPIX, CDELT) for sky coordinates
- Float or 32-bit int pixel data

PNG export:
- Apply a stretch (linear, log, or asinh) to map float → 8/16-bit
- Single band or false-color composite
- Save to user data / exports directory

**Acceptance:**
- FITS file opens correctly in DS9 / astropy (valid format)
- PNG is viewable and shows the image with reasonable stretch
- Metadata present in FITS headers

---

### Task 10a.6 — Observation: Real Instrument Integration

Update the session system to use the real instrument and physical SNR.

**Files:**
- Update `src/observation/observation_session.cpp`
- Update `src/observation/session_scheduler.cpp`

Replace the mock SNR formula in `ObservationSession::tick()`:
```cpp
// OLD (mock): snr_gain = 5.0 * dt_hours

// NEW: physical SNR
SNRParameters params = build_params(
    m_params,           // session: target, technique, band
    instrument,         // array: aperture, stations, efficiency
    universe);          // target flux, sky background

m_progress.accumulated_snr = SNRCalculator::compute_snr_cumulative(
    params, m_progress.elapsed_hours * 3600.0);
```

The session now tracks:
- Which instrument (ArrayInstrument)
- Which bands are active
- Accumulating integration time → physical SNR

The session produces a DataRecord AND can produce a MultispectralImage
(via the image formation engine) when it completes or when previewed.

**Acceptance:**
- Sessions now use physical SNR (faint targets take much longer)
- A bright star reaches detection quickly; a faint galaxy takes hours/days
- SNR matches the SNRCalculator predictions

---

### Task 10a.7 — Tab Content: Imaging

Implement the Imaging tab (was a placeholder in Sprint 09).

**Files:**
- `src/ui/tabs/imaging_tab.hpp`
- `src/ui/tabs/imaging_tab.cpp`

Replace the Sprint 09 placeholder with the real Imaging tab.

**Layout:**
```
┌──────────────────────────────────────────────────────────────────┐
│ IMAGING — Glasswing Array                                         │
│ ─────────────────────────────────────────────────────────────     │
│ Target: HIP 32005 (RA 06h45m Dec -16°42')   [CHANGE TARGET]       │
│                                                                    │
│ ┌─────────────────────────┬────────────────────────────────────┐ │
│ │  LIVE PREVIEW           │  CONFIGURATION                      │ │
│ │  (schematic)            │                                     │ │
│ │  ┌───────────────────┐  │  Stations (3/4 active):            │ │
│ │  │      ·  ·         │  │   ✓ Tycho Primary (12m)            │ │
│ │  │    ·   ⊕   ·      │  │   ✓ La Palma (8m)                  │ │
│ │  │       ·   ·       │  │   ✓ Mauna Kea (10m)                │ │
│ │  │  FOV: 60"         │  │   ✗ Paranal (8m)                   │ │
│ │  └───────────────────┘  │                                     │ │
│ │                         │  Bands (2 active):                  │ │
│ │  INTEGRATED IMAGE       │   ✓ Visible (550nm)                │ │
│ │  (builds over time)     │   ✓ Near-IR (1600nm)               │ │
│ │  ┌───────────────────┐  │   🔒 Mid-IR (locked)               │ │
│ │  │   [noise+signal]  │  │   🔒 Radio-K (locked)              │ │
│ │  │                   │  │                                     │ │
│ │  │      ◦            │  │  Resolution: 11.5 mas (visible)    │ │
│ │  │                   │  │  Collecting area: 232 m²           │ │
│ │  └───────────────────┘  │                                     │ │
│ │  Band: [Visible ▼]      │  Integration:                       │ │
│ │  Stretch: [asinh ▼]     │   Elapsed: 2.3 h                   │ │
│ │                         │   SNR: 24.5                         │ │
│ │                         │   [████████░░] target SNR 30        │ │
│ │                         │                                     │ │
│ │                         │  [START] [PAUSE] [STOP]            │ │
│ │                         │  [SAVE FITS] [SAVE PNG]            │ │
│ └─────────────────────────┴────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

**Functionality:**
- Target selection: from current Encyclopedia/Planetarium selection, or CHANGE TARGET
- Station toggles: activate/deactivate stations (affects SNR + collecting area)
- Band toggles: activate unlocked bands
- Live preview: schematic of the field with FOV box, target marker
- Integrated image: the forming image, with band selector and stretch control
- Integration controls: START begins a session, PAUSE/STOP control it
- SNR progress bar with target SNR
- Save buttons: FITS (all bands) and PNG (current band/composite)

The integrated image updates in real-time as the session accumulates SNR.
Rendered via the image formation engine, displayed as a Vulkan texture.

**State to persist:**
- Current target
- Station/band active states
- Selected display band, stretch mode
- Active session reference

**Acceptance:**
- Can configure stations and bands
- Live preview shows the field
- Starting integration creates a session; image builds over time
- SNR bar fills realistically (slow for faint targets)
- Save FITS and PNG work
- Band selector switches displayed band
- All within the tab's viewport (works in split-screen)

---

### Task 10a.8 — Integration & Mock Replacement

Wire the real instrument into the application, retire the mock.

**Files:**
- Update `src/core/application.cpp`
- Update `src/ui/tabs/encyclopedia_tab.cpp` (OBSERVE button now functional)
- Update sidebar instrument list
- DELETE `src/instruments/mock_instrument.*` and `src/analysis/mock_analyzer.*`

Changes:
- Application owns an ArrayInstrument (the Glasswing Array) instead of MockInstrument
- The Encyclopedia OBSERVE button: opens Imaging tab with that target pre-set
- Sidebar shows the array with active station count and status
- Sessions use the real instrument + physical SNR
- When a session completes, the analyzer (now real) extracts properties
  from the achieved SNR and the formed image:
  - SNR thresholds still gate knowledge levels, but now SNR is physical
  - Image-based detection: an object must be above noise floor in the image

Replace MockAnalyzer with a real `ImageAnalyzer`:
- Takes the DataRecord + MultispectralImage
- Detects sources above noise threshold (simple peak detection)
- For the target: extracts photometry (flux → magnitude per band)
- Multispectral: color from band ratios
- Knowledge updates based on what was measurable at the achieved SNR

**Acceptance (Sprint 10a Definition of Done):**
- [ ] Mock instrument fully replaced by ArrayInstrument
- [ ] Default Glasswing Array: 4 stations (Moon + 3 Earth), 2 active bands
- [ ] Physical SNR model: faint targets take realistically long
- [ ] Station toggles affect SNR and collecting area
- [ ] Band toggles work (locked bands shown but not usable)
- [ ] Imaging tab: live schematic preview + integrated image
- [ ] Integrated image forms realistically (noise → signal over time)
- [ ] PSF applied (point sources have correct angular size)
- [ ] Multispectral: per-band images, band selector works
- [ ] Stretch modes (linear/log/asinh) work
- [ ] FITS export produces valid multi-band file
- [ ] PNG export produces viewable image
- [ ] Encyclopedia OBSERVE button opens Imaging with target set
- [ ] Sessions use physical SNR; knowledge unlocks based on real measurements
- [ ] Sidebar shows array status
- [ ] Save/load preserves instrument config
- [ ] All previous features work (no regressions)
- [ ] ≥ 60fps
- [ ] No Vulkan validation errors

---

## Task Order

```
10a.1 → 10a.2 → 10a.3 → 10a.4 → 10a.5 → 10a.6 → 10a.7 → 10a.8
 types   snr    psf/img  formation export session  imaging integrate
                          engine            tab
```

---

## Deliberate Simplifications (revisited in 10b and later)

- **Total-power mode only**: stations sum as one aperture. No baselines, no
  interferometry, no aperture synthesis. Angular resolution = single largest
  aperture diffraction limit. (10b adds interferometry.)
- **Simultaneous multiband**: all active bands observed at once. Real arrays may
  time-share; we simplify.
- **No atmospheric seeing on the image yet**: Earth stations have higher noise
  (system temperature) but we don't simulate seeing-induced PSF blurring in 10a.
  (Could add in 10b or later.)
- **Simple source detection**: peak-finding above noise. No deblending, no
  sophisticated photometry. (Later sprints refine.)
- **Sub-resolution objects as point sources**: galaxies smaller than the PSF
  appear as points. True resolution in 10b.
- **No flat fields, darks, calibration frames**: the image is the "calibrated"
  result directly. (Astrophotography pipeline is a later sprint.)

---

## Physics Reference (for implementation accuracy)

**Radiometer equation (the heart of SNR):**
```
SNR = S·t / sqrt((S + B)·t + σ_read²·N)

S = source count rate (e⁻/s) = flux · area · efficiency · bandwidth · throughput
B = background count rate (e⁻/s)
t = integration time (s)
σ_read = read noise per read (e⁻)
N = number of reads
```

**Diffraction limit:**
```
θ_min = 1.22 · λ / D   (radians, for a circular aperture of diameter D)
```

**Magnitude to flux (AB system):**
```
m_AB = -2.5 · log10(f_ν / 3631 Jy)
f_ν = 3631 · 10^(-0.4 · m_AB)  Jy
```

**Collecting area:**
```
A = π · (D/2)²   per station
A_total = Σ A_i   for active stations (total-power mode)
```

These are real formulas. Using them makes the gameplay physically grounded:
the player genuinely learns why faint objects need long integrations,
why aperture matters, why more stations help.
