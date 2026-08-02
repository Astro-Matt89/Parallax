# Parallax — Optical Imaging Trio Specification (rev. 2)

**Status:** normative draft for Copilot implementation (sprint slot after 10b)
**Prerequisite:** Sprint 10a complete (ArrayInstrument, physical SNR, Imaging tab)
**Oracle:** `glasswing-sandbox-v1_5.html` — IMAGING 10a module, telescope presets, sensor view
**Goal:** Classic single-dish optical imaging via three telescopes spanning three decades
of aperture. The player selects a telescope; framing, sampling and PSF change accordingly.
NO interferometry here; NO change to ArrayInstrument.

---

## Design Rationale (read first)

The trio is a **logarithmic ladder** in aperture (0.28 m → 8 m → 100 m, ×~30 per step)
and the inverse in field of view (degrees → arcminutes → arcseconds). Each rung is a
different game experience from the same sky position:

- **GW-UWF** — 0.28 m survey scope. Discovery/monitoring; feeds the procedural catalog.
  Degrees of field. This is the instrument with the **DSS-style immersion preview**
  (field stars, diffraction spikes) — see the non-normative section at the end.
- **GW-NF** — 8 m (VLT-class) deep-sky workhorse. Arcminutes. The mid-progression
  instrument for galaxies, nebulae, planetaries; needs AO to approach its diffraction limit.
- **GW-OWL** — 100 m **OWL-class** (the ESO OverWhelmingly Large Telescope concept:
  segmented spherical primary, native f/6, AO science focus). Reaches **~1 mas in V**,
  a factor ~40 over HST. Endgame instrument for stellar surfaces and exoplanet features.

**Crucial pedagogical bridge to 10b:** even the 100 m OWL at ~1 mas does **not** beat
the interferometer's angular resolution (λ/B_max with continental/Earth–Moon baselines
reaches microarcseconds). This is historically accurate (it was stated in the OWL science
case) and it is *the* reason aperture synthesis exists in Parallax. The single-dish trio
tops out where the interferometer begins. Surface this in-game when the player maxes the
OWL and still can't resolve a target the array can.

---

## Normative Constants — The Trio

| id  | code    | role                     | D [m]  | f [m]  | f/#  | pixel [µm] | sensor [mm]   |
|-----|---------|--------------------------|--------|--------|------|------------|---------------|
| uwf | GW-UWF  | survey ultra-wide field  | 0.28   | 0.56   | 2.0  | 3.76       | 36.0 × 24.0   |
| nf  | GW-NF   | deep-sky, 8 m VLT-class  | 8.0    | 32.0   | 4.0  | 15.0       | 61.4 × 61.4   |
| unf | GW-OWL  | OWL-class 100 m          | 100.0  | 6000.0 | 60.0 | 15.0       | 61.4 × 61.4   |

Notes on the OWL focal chain: the native telescope is **f/6.0** (100 m primary, OWL
4-mirror concept). The imaging camera operates at an **effective f/60** — a ×10 adaptive
relay at the science focus that Nyquist-samples the ~1 mas diffraction limit. Model the
camera plate scale (f/60); the native f/6 is flavor for the encyclopedia, not used in
the PSF math. GW-NF likewise uses an f/4 wide-imager focus on the 8 m aperture.

Derived values (MUST match to 4 significant digits — unit tests assert these, at λ=550 nm):

| id  | plate scale    | FOV            | θ_diff (1.03 λ/D) | diffraction sampling |
|-----|----------------|----------------|-------------------|----------------------|
| uwf | 1.385 ″/px     | 3.68° × 2.46°  | 0.417 ″           | 0.30 px/FWHM (plate-under) |
| nf  | 0.0967 ″/px    | 6.60′ × 6.60′  | 14.61 mas         | 0.15 px/FWHM (plate-under) |
| unf | 0.516 mas/px   | 2.11″ × 2.11″  | 1.17 mas          | 2.27 px/FWHM (Nyquist OK)  |

Read the sampling column carefully: UWF and NF are **plate-scale-limited** at these
cameras (the diffraction core is far finer than a pixel, so at native focus they are
seeing/plate limited — realistic for survey and wide deep-sky cameras). Only the OWL
science camera is built to Nyquist-sample its own diffraction limit. The sampling verdict
in the UI must be computed from the **rendered PSF FWHM** (see below), not assumed.

Gameplay mapping: uwf = discovery/survey (first telescope, DSS immersion), nf = deep-sky
workhorse (mid), unf = mas-scale endgame (AO-critical; the "why interferometry" teacher).

---

## Normative Formulas

All angles in **radians** internally. Display conversion: 1 rad = 206264.806 arcsec.

```
plate_scale_rad_per_px = pixel_pitch_m / focal_m                      // (1)
plate_scale_arcsec     = 206264.806 * pixel_pitch_m / focal_m         // (2)
fov_rad                = sensor_dim_m / focal_m                       // (3)
theta_diff_rad         = 1.22 * lambda_m / aperture_m                 // (4) first Airy null
fwhm_diff_rad          = 1.03 * lambda_m / aperture_m                 // (5) diffraction FWHM
r0_m                   = 0.98 * lambda_m / seeing_fwhm_rad            // (6) Fried parameter
```

**PSF via MTF product** (frequency f in cycles/rad, cutoff f_c = D/λ):

```
MTF_tel(f) = (2/π)·(acos(x) − x·sqrt(1−x²)),  x = f/f_c,  0 for x ≥ 1   // (7)
MTF_atm(f) = exp(−3.44·(λ·f/r0)^(5/3))        // (8) Kolmogorov, long exposure
MTF_ao(f)  = S + (1−S)·MTF_atm(f)             // (9) partial AO, Strehl S (0.4 default)
MTF(f)     = MTF_tel(f) · MTF_ao(f)           // space/Moon: MTF_ao ≡ 1
PSF        = IFFT2( MTF sampled on the sensor grid )                   // (10)
```

Sensor grid sampling of (10): frequency step `du = 1/(Npix · plate_scale_rad)`.
MTF(0)=1 ⇒ Σ PSF = 1 (flux normalization is free — assert it).

**Point-source shortcut (normative):** when `theta_obj < 0.1 · plate_scale_rad` the
target is unresolved at sensor scale; its visibility at all sensor frequencies is ≈ total
flux, so `sensor_image = flux · PSF`. This holds for all sandbox targets on UWF/NF and for
most on the OWL; extended catalog DSOs may NOT satisfy it — in that case sample the target
FT at the sensor frequencies (bilinear, exactly as ArrayInstrument does on the uv grid).
Implement the criterion, not just the shortcut.

**Sampling verdict** (from the measured FWHM of the rendered PSF, in px — NOT formula (5)):

```
px_per_fwhm < 2.0        → UNDERSAMPLED   (warn)
2.0 ≤ px_per_fwhm ≤ 3.5  → NYQUIST OK     (ok)
px_per_fwhm > 3.5        → OVERSAMPLED    (info)
```

---

## Architecture

```cpp
// src/instruments/optical_telescope.h
struct OpticalTelescope {
    std::string id;            // "uwf" | "nf" | "unf"
    std::string code;          // "GW-UWF" | "GW-NF" | "GW-OWL"
    double aperture_m, focal_m, pixel_pitch_m, sensor_w_m, sensor_h_m;
    double plateScaleRad() const { return pixel_pitch_m / focal_m; }
    double fovWRad()      const { return sensor_w_m   / focal_m; }
    double fovHRad()      const { return sensor_h_m   / focal_m; }
};
const std::array<OpticalTelescope,3>& opticalTrio();

// src/instruments/psf_engine.h
struct PsfParams { double lambda_m, aperture_m, seeing_fwhm_rad /*0→diff*/, strehl /*0→no AO*/; };
double computePsf(const PsfParams&, double plate_scale_rad, int n, std::span<float> psf);

// src/instruments/single_dish_instrument.h  — sibling of ArrayInstrument
class SingleDishInstrument : public Instrument {
    // owns OpticalTelescope selection + PsfParams; produces
    // SensorFrame{ image, fwhm_px, plate_scale_rad, telescope_id }
};
```

Task order (do NOT reorder; later tasks import earlier ones):

```
Task 1: optical_telescope.{h,cpp}       — trio table + derived getters + unit tests
Task 2: psf_engine.{h,cpp}              — formulas (7)-(10), reuse 10a FFT
Task 3: single_dish_instrument.{h,cpp}  — Instrument subclass, SensorFrame
Task 4: ui/tabs/imaging_tab.cpp         — telescope selector, framing readouts
Task 5: tests/test_optical_trio.cpp     — fixture cross-validation (below)
Task 6: integration                     — sidebar entry, save/load of selection
```

---

## Cross-Validation with the Sandbox (Task 5)

Single-target datasets carry an `imaging10a` block (dataset schema ≥ "1.2"; the frozen
15-fixture battery v1.1 does NOT contain it — do not look for it there):

```json
"imaging10a": {
  "apertureM": ..., "seeing": "diff|0.4|0.8|1.5", "adaptiveOptics": bool,
  "strehl": ..., "peakSnr": ..., "thetaDiffRad": ..., "psfFwhmRad": ...,
  "telescope": "custom|uwf|nf|unf", "view": "ang|sens",
  "plateScaleRadPerPx": ..., "sensorFwhmRad": ...,
  "sensorImage": [...], "image": [...], "psf": [...]
}
```

Tolerances (same philosophy as the 10b contract):
- derived constants (plate scale, FOV, θ_diff): relative 1e-4
- PSF arrays vs `sensorImage`/`psf` (noiseless, peakSnr=0): abs 1e-6 of peak
- measured FWHM vs `sensorFwhmRad`: relative 1e-2 (interpolated measurement)
- flux conservation Σ image / flux = 1 within 1e-9 (noiseless)

Noise is validated statistically only (different RNG in C++): sample std within 5% of
peak/SNR over the frame.

---

## NON-NORMATIVE — UWF Immersion Preview (DSS-style)

**This is a visualization layer, not part of the instrument oracle.** It exists to preview
how Parallax graphics could look. It MUST NOT feed `SensorFrame` numbers used by the
analyzer, MUST NOT enter datasets, and is NEVER cross-validated numerically. Implement it
in the render layer (e.g. `ui/render/immersive_field.cpp`), keyed to GW-UWF only.

The sandbox reference builds it as follows (reproduce the *look*, not exact pixels):

- **Two-population procedural field**, seeded from the target seed (stable per target):
  - *Hero stars*: 12–21 bright stars, magnitude 4.5–10 weighted toward the faint end
    (`m = 4.5 + 5.5·u^0.6`). These get halos and diffraction spikes.
  - *Faint carpet*: a density-controlled population (400 / 900 / 1800), magnitude 11–16
    via the Euclidean count law `dN/dm ∝ 10^(0.6 m)` (inverse-CDF sampling). Texture only.
  - Colors from a B−V → RGB tint; flux = 10^(−0.4(m − 4.5)).
- **Rendering**: each star splatted as a Gaussian of FWHM = seeing / plate_scale (px).
  Stars with flux > 0.015 add a soft exponential **halo** and 4-arm **diffraction spikes**
  (× orientation; spike length ∝ log(1 + flux)). Faint carpet stars are bare splats.
- **Target in context** at frame center (unresolved in UWF → a colored point with its own
  spikes) plus a faint marker ring so the player can find the catalogued object.
- **Sky + grain**: low uniform background with per-pixel photographic grain.
- **Stretch**: common-max **asinh** with a high saturation factor + gamma 0.85, so bright
  cores clip to white with blooming — the DSS photographic-plate look.

In Parallax this is where you'd later layer real rendering (extended DSO sprites from the
catalog, galactic-latitude-dependent density, dust). The contract is only: it never
influences measured photometry or knowledge unlocks.

---

## Common Pitfalls

```
❌ DO NOT give each telescope its own sky render — one truth grid, three samplings
❌ DO NOT compute the PSF in image space from an Airy formula — use MTF product + IFFT
❌ DO NOT use formula (5) for the sampling verdict — measure FWHM on the rendered PSF
❌ DO NOT apply the point-source shortcut without testing the criterion (extended DSOs!)
❌ DO NOT let the DSS immersion field touch SensorFrame, photometry, datasets, or tests
❌ DO NOT model the OWL PSF at native f/6 — the imaging camera is f/60 (AO relay)
❌ DO NOT touch ArrayInstrument, uv machinery, or CLEAN
❌ DO NOT store angles in arcsec internally — radians everywhere, convert at display
✅ DO assert Σ PSF = 1 after IFFT (MTF(0)=1 makes this free)
✅ DO reuse the 10a FFT utilities — no new FFT implementation
✅ DO keep telescope constants in ONE table (the trio array), never inline
✅ DO surface the "OWL can't beat the interferometer" moment — it motivates 10b
```

---

## Definition of Done

- [ ] `opticalTrio()` returns the three telescopes; derived values match the table to 4 s.d.
- [ ] PSF engine implements (7)–(10); Σ PSF = 1 asserted
- [ ] Diffraction-limited on Moon site; seeing + optional AO on Earth sites
- [ ] SingleDishInstrument produces SensorFrame at correct plate scale per telescope
- [ ] Imaging tab: telescope selector; readouts for focal, f/#, plate scale (mas-aware), FOV, px/FWHM verdict
- [ ] Verdict thresholds exactly 2.0 / 3.5 px/FWHM, from measured PSF FWHM
- [ ] Point-source criterion implemented (0.1 · plate_scale); extended path falls back to FT sampling
- [ ] OWL camera modeled at f/60; native f/6 only in encyclopedia text
- [ ] DSS immersion preview renders for GW-UWF only and is provably isolated from photometry/datasets
- [ ] Fixture cross-validation passes at the tolerances above against sandbox v1.5 exports
- [ ] Save/load preserves telescope selection
- [ ] No regressions in 10a/10b features; ≥ 60fps; no Vulkan validation errors
