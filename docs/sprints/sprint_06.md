# Sprint 06 — Solar System & Atmosphere Toggle

**Prerequisite:** Sprint 05 complete (Tycho-2, interactive UI, constellations, Messier, grids)
**Goal:** Sun, Moon, and major planets in the skychart; atmosphere on/off toggle for planning.
**Deliverable:** Planets visible as colored points, Moon with phase, Sun position driving twilight/daylight, atmosphere toggle revealing full sky regardless of conditions.

---

## Overview

The Solar System is the most fundamental missing piece in the skychart.
Without it, the observer cannot:
- See when twilight begins/ends (Sun position)
- Know if the Moon will interfere with observations (Moon position + phase)
- Locate the brightest naked-eye targets (Jupiter, Saturn, Mars, Venus)
- Plan observations around daylight and moonlight

This sprint also adds the atmosphere toggle — a critical planning tool that lets
the observer see ALL objects regardless of daylight, moonlight, or sky brightness.

---

## Tasks

### Task 6.1 — Astro: Solar Ephemeris

Compute the Sun's position (RA/Dec) for any Julian Date.

**Files:** `src/astro/solar_system.hpp`, `src/astro/solar_system.cpp`

**Interface:**
```cpp
namespace parallax::astro
{
    struct CelestialBodyState
    {
        EquatorialCoord equatorial;     // RA/Dec (geocentric, J2000)
        HorizontalCoord horizontal;     // Alt/Az (observer-dependent, computed separately)
        f64 distance_au;                // Distance from Earth (AU)
        f32 magnitude;                  // Apparent visual magnitude
        f32 angular_diameter_arcsec;    // Apparent angular size
        f32 phase_angle_deg;            // Phase angle (Sun-Body-Earth)
        f32 illumination;               // Fraction illuminated (0..1)
    };

    class SolarSystem
    {
    public:
        /// Compute Sun position for given JD
        [[nodiscard]] static CelestialBodyState compute_sun(f64 jd);

        /// Compute Moon position for given JD
        [[nodiscard]] static CelestialBodyState compute_moon(f64 jd);

        /// Compute planet position for given JD
        /// planet_id: 1=Mercury, 2=Venus, 4=Mars, 5=Jupiter, 6=Saturn, 7=Uranus, 8=Neptune
        [[nodiscard]] static CelestialBodyState compute_planet(f64 jd, u32 planet_id);

        /// Compute all bodies at once (more efficient, shared intermediate values)
        struct AllBodies
        {
            CelestialBodyState sun;
            CelestialBodyState moon;
            std::array<CelestialBodyState, 7> planets;  // Mercury..Neptune (no Earth)
        };
        [[nodiscard]] static AllBodies compute_all(f64 jd);

    private:
        // Low-precision solar coordinates (Meeus Ch. 25)
        static void compute_sun_geometric(f64 jd, f64& L0, f64& M, f64& e);

        // Ecliptic to equatorial conversion
        static EquatorialCoord ecliptic_to_equatorial(f64 lambda_rad, f64 beta_rad, f64 epsilon_rad);

        // Mean obliquity of the ecliptic
        static f64 mean_obliquity(f64 jd);
    };
}
```

**Sun position algorithm (Meeus, Ch. 25 — low precision, ~0.01° accuracy):**
```
T = Julian centuries from J2000.0

Geometric mean longitude:
  L0 = 280.46646 + 36000.76983 × T + 0.0003032 × T²  [degrees]

Mean anomaly:
  M = 357.52911 + 35999.05029 × T - 0.0001537 × T²  [degrees]

Equation of center:
  C = (1.914602 - 0.004817×T - 0.000014×T²) × sin(M)
    + (0.019993 - 0.000101×T) × sin(2M)
    + 0.000289 × sin(3M)

Sun's true longitude:
  λ = L0 + C  [degrees]

Sun's true anomaly:
  ν = M + C

Distance (AU):
  R = 1.000001018 × (1 - e²) / (1 + e × cos(ν))
  Where e = 0.016708634 - 0.000042037×T - 0.0000001267×T²

Apparent longitude (corrected for nutation and aberration):
  Ω = 125.04 - 1934.136 × T
  λ_apparent = λ - 0.00569 - 0.00478 × sin(Ω)

Obliquity of ecliptic:
  ε₀ = 23.439291 - 0.013004×T - 0.000000164×T² + 0.000000504×T³
  ε = ε₀ + 0.00256 × cos(Ω)

Ecliptic → Equatorial:
  RA = atan2(cos(ε) × sin(λ), cos(λ))
  Dec = asin(sin(ε) × sin(λ))

Magnitude: -26.74 (constant for skychart purposes)
Angular diameter: 1919.26 / R  [arcseconds]
```

**Tests:** `tests/test_solar_system.cpp`
- Sun RA/Dec at J2000.0 (2000-01-01 12:00 UTC): RA ≈ 18h 45m, Dec ≈ -23° 02'
- Sun at vernal equinox (Mar 20): RA ≈ 0h, Dec ≈ 0°
- Verify against USNO or JPL Horizons for a recent date

**Acceptance:** Sun position accurate to < 0.02° compared to JPL Horizons.

---

### Task 6.2 — Astro: Lunar Ephemeris

Compute the Moon's position, phase, and illumination.

**Implementation:** Meeus Ch. 47 (simplified) or the more complete ELP2000 series.

For the skychart, Meeus simplified is sufficient (~0.1° accuracy).

**Moon position algorithm (Meeus, Ch. 47 — abridged):**
```
T = Julian centuries from J2000.0

Moon's mean longitude:
  L' = 218.3165 + 481267.8813 × T  [degrees]

Mean elongation:
  D = 297.8502 + 445267.1115 × T

Sun's mean anomaly:
  M = 357.5291 + 35999.0503 × T

Moon's mean anomaly:
  M' = 134.9634 + 477198.8676 × T

Moon's argument of latitude:
  F = 93.2720 + 483202.0175 × T

Longitude perturbations (main terms):
  Σl = +6288774 × sin(M')
       +1274027 × sin(2D - M')
       +658314 × sin(2D)
       +213618 × sin(2M')
       -185116 × sin(M)
       -114332 × sin(2F)
       +58793 × sin(2D - 2M')
       +57066 × sin(2D - M - M')
       +53322 × sin(2D + M')
       +45758 × sin(2D - M)
       ... (top 10-15 terms sufficient for skychart)

Latitude perturbations (main terms):
  Σb = +5128122 × sin(F)
       +280602 × sin(M' + F)
       +277693 × sin(M' - F)
       +173237 × sin(2D - F)
       +55413 × sin(2D - M' + F)
       +46271 × sin(2D - M' - F)
       ... (top 10 terms)

Distance perturbations:
  Σr = -20905355 × cos(M')
       -3699111 × cos(2D - M')
       -2955968 × cos(2D)
       -569925 × cos(2M')
       ... (top 10 terms)

Ecliptic coordinates:
  λ = L' + Σl / 1000000  [degrees]
  β = Σb / 1000000  [degrees]
  Δ = 385000.56 + Σr / 1000  [km]

Convert to equatorial (same as Sun, using ecliptic_to_equatorial).
```

**Moon phase and illumination:**
```
Phase angle:
  i = acos(cos(D) × cos(M') + sin(D) × sin(M') × cos(F))
  (simplified: i ≈ elongation angle from Sun)

More precisely:
  Elongation ψ = acos(sin(dec_sun)×sin(dec_moon) + cos(dec_sun)×cos(dec_moon)×cos(ra_moon - ra_sun))
  Phase angle i = atan2(R_sun × sin(ψ), Δ_moon - R_sun × cos(ψ))
  Illumination k = (1 + cos(i)) / 2

Phase name:
  New:              k < 0.02
  Waxing crescent:  0.02 ≤ k < 0.48
  First quarter:    0.48 ≤ k < 0.52
  Waxing gibbous:   0.52 ≤ k < 0.98
  Full:             k ≥ 0.98
  (mirror for waning, determined by whether elongation is increasing or decreasing)
```

**Apparent magnitude of Moon (approximate):**
```
V_moon ≈ -12.73 + 0.026 × |i| + 4e-9 × i⁴
Where i = phase angle in degrees
Full moon: ≈ -12.7
Quarter: ≈ -10.0
Crescent: ≈ -8.0
```

**Angular diameter:**
```
d = 2 × asin(1737.4 / Δ)  [radians, Δ in km]
Average: ~31 arcmin (about 0.52°)
```

**Tests:**
- Full moon: illumination ≈ 1.0
- New moon: illumination ≈ 0.0
- Moon position verified against JPL Horizons for a known date
- Moon always within ~5° of ecliptic

**Acceptance:** Moon position accurate to < 0.2°. Phase illumination correct.

---

### Task 6.3 — Astro: Planetary Ephemeris

Compute positions of Mercury through Neptune.

**Implementation:** Meeus Ch. 31-36 (planetary positions using orbital elements).

For skychart accuracy (~0.1°), simplified orbital elements with periodic perturbations.

**Algorithm (simplified Keplerian + perturbations):**
```
For each planet, compute orbital elements at epoch T:
  L = mean longitude
  a = semi-major axis (AU)
  e = eccentricity
  i = inclination
  Ω = longitude of ascending node
  ω̃ = longitude of perihelion

Solve Kepler's equation for eccentric anomaly E:
  M = L - ω̃  (mean anomaly)
  E - e × sin(E) = M  (iterate until convergence)

True anomaly:
  ν = 2 × atan2(sqrt(1+e) × sin(E/2), sqrt(1-e) × cos(E/2))

Heliocentric distance:
  r = a × (1 - e × cos(E))

Heliocentric ecliptic coordinates:
  x_h = r × (cos(Ω)×cos(ν+ω-Ω) - sin(Ω)×sin(ν+ω-Ω)×cos(i))
  y_h = r × (sin(Ω)×cos(ν+ω-Ω) + cos(Ω)×sin(ν+ω-Ω)×cos(i))
  z_h = r × sin(ν+ω-Ω) × sin(i)

Geocentric coordinates (subtract Earth's heliocentric position):
  x_g = x_h - x_earth
  y_g = y_h - y_earth
  z_g = z_h - z_earth

Geocentric ecliptic longitude and latitude:
  λ = atan2(y_g, x_g)
  β = atan2(z_g, sqrt(x_g² + y_g²))
  Δ = sqrt(x_g² + y_g² + z_g²)

Convert ecliptic → equatorial.
```

**Planet orbital elements:**
Use the polynomial expressions from Meeus Table 31.A (J2000.0 epoch).
These are standard and well-published. Store as constexpr arrays:

```cpp
struct OrbitalElements
{
    f64 L[4];      // Mean longitude coefficients (T⁰, T¹, T², T³)
    f64 a[2];      // Semi-major axis (constant + rate, AU)
    f64 e[4];      // Eccentricity
    f64 i[4];      // Inclination (degrees)
    f64 omega[4];  // Longitude of ascending node (degrees)
    f64 pi[4];     // Longitude of perihelion (degrees)
};

static constexpr OrbitalElements kMercury = { ... };
static constexpr OrbitalElements kVenus = { ... };
// ... etc for each planet
```

**Apparent magnitudes (approximate formulas):**
```
Mercury: V = -0.42 + 5log(rΔ) + 0.038×i - 0.000273×i² + 0.000002×i³
Venus:   V = -4.40 + 5log(rΔ) + 0.0009×i + 0.000239×i²
Mars:    V = -1.52 + 5log(rΔ) + 0.016×i
Jupiter: V = -9.40 + 5log(rΔ) + 0.005×i
Saturn:  V = -8.95 + 5log(rΔ)  (varies with ring tilt, simplified)
Uranus:  V = -7.19 + 5log(rΔ)
Neptune: V = -6.87 + 5log(rΔ)
Where r = helio distance, Δ = geo distance, i = phase angle (degrees)
```

**Tests:**
- Jupiter and Saturn positions verified against JPL Horizons
- Venus near inferior/superior conjunction: verify it switches from evening to morning sky
- Mars at opposition: verify RA is ~opposite the Sun
- All planets should lie near the ecliptic (within ~8°)

**Acceptance:** Planet positions accurate to < 0.5° for all planets. Magnitudes reasonable.

---

### Task 6.4 — Astro: Twilight and Sky State

Determine sky state based on Sun altitude.

**Files:** Update `src/astro/atmosphere.hpp/cpp` or create `src/astro/sky_state.hpp`

**Interface:**
```cpp
namespace parallax::astro
{
    enum class SkyState
    {
        Day,                // Sun alt > 0°
        CivilTwilight,      // Sun alt -6° to 0°
        NauticalTwilight,   // Sun alt -12° to -6°
        AstroTwilight,      // Sun alt -18° to -12°
        Night               // Sun alt < -18°
    };

    struct SkyConditions
    {
        SkyState state;
        f32 sun_altitude_deg;
        f32 moon_altitude_deg;
        f32 moon_illumination;          // 0..1
        f32 moon_sky_brightness_mag;    // Additional sky brightness from Moon
        f32 effective_limiting_mag;     // Naked-eye limit given conditions
        f32 sky_brightness_zenith;      // mag/arcsec² at zenith
    };

    class SkyConditionCalculator
    {
    public:
        [[nodiscard]] static SkyConditions compute(
            const CelestialBodyState& sun,
            const CelestialBodyState& moon,
            const ObserverLocation& observer,
            f64 lst_rad,
            f32 bortle_scale
        );
    };
}
```

**Sky brightness model with Moon:**
```
Base brightness: from Bortle scale (existing)

Moon contribution (when above horizon):
  V_moon_sky ≈ f(moon_altitude, moon_phase, angular_distance_from_moon)

Simplified:
  If moon is below horizon: no contribution
  If moon is above horizon:
    delta_SB ≈ -2.5 × log10(1 + 10^(0.4 × (21.5 - V_moon + 5×log10(Δθ/30))))
    Where Δθ = angular distance from Moon in degrees

For the skychart, a simpler model is fine:
  Full moon above horizon: limiting mag drops by ~2-3 magnitudes
  Quarter moon: drops by ~1-1.5 magnitudes
  New/below horizon: no effect
```

**Twilight sky color (for sky_background shader):**
```
sun_alt > 0°:       Bright blue sky (day)
0° to -6°:          Orange/pink at horizon, darkening overhead
-6° to -12°:        Deep blue, fading warm horizon
-12° to -18°:       Very faint glow, nearly dark
< -18°:             Full night (existing rendering)
```

**Acceptance:**
- Sky state correctly transitions through twilight stages
- Moon contribution to sky brightness is reasonable
- Effective limiting magnitude drops with moonlight

---

### Task 6.5 — Rendering: Solar System in Skychart

Render Sun, Moon, and planets in the skychart as schematic objects.

**Files:**
- `src/rendering/solar_system_renderer.hpp`, `src/rendering/solar_system_renderer.cpp`

**Rendering approach (SKYCHART — schematic, not realistic):**

**Sun:**
- Yellow-orange filled circle, size ~8px
- Small ray/cross pattern extending outward (optional)
- Label: "Sun" or "☉"
- Always rendered even during day (useful with atmosphere off)

**Moon:**
- White/gray filled circle, size ~6px
- Phase indicator: draw the illuminated portion
  (simple approach: draw full circle, then overlay shadow arc for phase)
- Label: "Moon" or "☽" + phase name ("Full", "Wax Gib", etc.)

**Planets:**
- Colored filled circles, size based on magnitude (same scaling as stars)
- Mercury: gray-white
- Venus: bright white-yellow
- Mars: orange-red
- Jupiter: cream/tan
- Saturn: pale yellow (optional: tiny line for rings indication)
- Uranus: pale cyan
- Neptune: pale blue
- Labels: planet name

**All solar system objects:**
- Use same coordinate transform pipeline as stars (project_radec_to_screen)
- Subject to horizon culling (unless atmosphere is off)
- Selectable — clicking shows info in the info panel
- Always visible regardless of magnitude limit setting

**Render order:** Solar system objects render AFTER starfield, BEFORE constellation lines.
This ensures planets appear on top of background stars.

**Acceptance:**
- All planets visible at correct positions
- Venus and Jupiter are the brightest
- Moon shows correct phase
- Sun at correct position (can verify: should be below horizon at night)
- Clicking a planet shows info panel with name, RA/Dec, magnitude, distance
- Planets move perceptibly at ×1000 time speed

---

### Task 6.6 — Rendering: Twilight Sky Background

Update the sky background shader to respond to Sun altitude.

**Changes to:** `src/rendering/sky_background.cpp`, `shaders/sky_background.frag`

**Updated sky model:**
```
Inputs to shader (via uniform buffer):
  - sun_altitude_deg (NEW)
  - sun_azimuth_deg (NEW — for directional twilight glow)
  - moon_altitude_deg (NEW)
  - moon_illumination (NEW)
  - bortle_scale (existing)
  - camera pointing (existing)
  - atmosphere_enabled (NEW — toggle)

If atmosphere_enabled == false:
  Sky is pure black everywhere. No gradient. No twilight.
  Stars/planets visible regardless.

If atmosphere_enabled == true:
  Night (sun < -18°):
    Existing gradient (Bortle-based, darker zenith, brighter horizon)
    + Moon glow if Moon is above horizon (additive warm white near Moon position)

  Astronomical twilight (-18° to -12°):
    Faint warm glow on horizon in Sun's direction
    Rest of sky nearly dark

  Nautical twilight (-12° to -6°):
    Deeper blue overall
    Warm orange/pink band at Sun's horizon

  Civil twilight (-6° to 0°):
    Bright blue sky with intense orange/pink at Sun's horizon
    Stars still faintly visible overhead at start, invisible by end

  Day (sun > 0°):
    Bright blue sky, uniform
    No stars visible (they're drowned out)
    Sun position rendered

Transition: smooth blending between states based on sun_altitude.
```

**Implementation note:**
For the skychart, the twilight rendering does NOT need to be physically accurate.
A parametric blend based on sun altitude is sufficient. The accurate Rayleigh/Mie
scattering model is reserved for the imaging mode.

**Acceptance:**
- Sunset/sunrise transitions visible when time passes through dusk/dawn
- Twilight glow appears in the direction of the Sun
- At ×100 or ×1000 time speed, day/night cycle is clearly visible
- Moon glow adds faint brightening near Moon position
- Atmosphere toggle OFF: pure black sky at all times

---

### Task 6.7 — UI: Atmosphere Toggle + Solar System Info

Integrate atmosphere toggle and solar system into the UI.

**Changes:**
- Toolbar: add ATMO toggle button
- Keyboard: `A` key toggles atmosphere
- Side panel: atmosphere toggle option
- HUD: show sky state ("NIGHT", "CIVIL TWI", "DAY", "ATMO OFF")
- Info panel: handle solar system body selection

**Atmosphere toggle behavior:**
```
When atmosphere is ON (default):
  - Sky background shows gradient, twilight, moonlight
  - Horizon culling active (stars below horizon hidden)
  - Sky state shown in HUD
  - Effective limiting magnitude affected by Moon/twilight

When atmosphere is OFF:
  - Sky is pure black
  - Horizon culling DISABLED — stars below horizon visible
  - All objects visible regardless of Sun/Moon
  - HUD shows "ATMO OFF"
  - Magnitude limit is the ONLY visibility control
  - Useful for: planning during full moon, daytime target identification,
    seeing what's below the horizon (circumpolar calculations)
```

**Solar system info panel (when selected):**
```
┌─── SELECTED ──────────────┐
│                            │
│ ♃ JUPITER                  │
│                            │
│ RA    22h 15m 33.2s        │
│ Dec   -11° 42' 15.8"      │
│                            │
│ Alt   +38° 12' 44"        │
│ Az    195° 23' 12"        │
│                            │
│ Vmag    -2.31              │
│ Distance  5.12 AU          │
│ Ang.Size  38.2"            │
│ Phase     99.4%            │
│                            │
│ Constellation  Aqr         │
│                            │
│ ──────────────────────     │
│ [GOTO]  [TRACK]  [INFO]   │
│                            │
└────────────────────────────┘
```

For Moon, also show: phase name, illumination %, next full/new moon date (optional).

**Acceptance (Sprint 06 Definition of Done):**

### 6.1 SolarSystem astro module
- [x] `compute_all`, `compute_moon_full`, `compute_sun` — Mercury–Neptune + Sun + Moon positions.
  *Implemented in PR #12 — see `src/astro/solar_system.hpp`, `src/astro/solar_system.cpp`.*

### 6.2 Atmosphere model parameters
- [x] Extinction and refraction parameters present in `AtmosphereParams`.
  *Pre-existing in `src/astro/atmosphere.hpp` from Sprint 03 Task 3.3. Dormant in skychart mode; reserved for imaging mode.*

### 6.3 Twilight conditions parametric model
- [x] Sky background shader branches on sun altitude: NIGHT / ASTRO TWI / NAUTICAL TWI / CIVIL TWI / DAY.
  *Implemented in PR #14 — see `src/rendering/sky_background.cpp`, `shaders/sky_background.frag`.*

### 6.4 Moon-glow contribution to sky brightness
- [x] Moon altitude + illumination passed to sky shader; additive glow applied near Moon position.
  *Implemented in PR #14 — see `src/rendering/sky_background.cpp`.*

### 6.5 SolarSystemRenderer
- [x] Draws Sun/Moon/planets as schematic icons; horizon culling on when atmosphere_on=true, all-visible when atmosphere_on=false; clickable via Selection system.
  *Implemented in PR #12 — see `src/rendering/solar_system_renderer.hpp/.cpp`.*
  *atmosphere_on plumbed from Application::m_atmosphere_on in this PR (Task 6.7) — see `src/core/application.cpp`.*

### 6.6 Sky background shader
- [x] Responds to Sun altitude (twilight bands) and Moon (additive glow); smooth transitions; pure black when atmosphere_on=false.
  *Implemented in PR #14 — see `src/rendering/sky_background.cpp`, `shaders/sky_background.frag`.*

### 6.7 UI integration (this PR)
- [x] Toolbar ATMO toggle button — `src/ui/toolbar.hpp`, `src/ui/toolbar.cpp`.
- [x] A-key binding calls `toggle_atmosphere()` — `src/core/application.cpp`.
- [x] `is_atmosphere_on()` / `toggle_atmosphere()` / `set_atmosphere()` accessors — `src/core/application.hpp/.cpp`.
- [x] HUD sky-state readout (SKY: NIGHT / CIVIL TWI / NAUTICAL TWI / ASTRO TWI / DAY / ATMO OFF) — `src/ui/hud.hpp`, `src/ui/hud.cpp`.
- [x] Frame-loop canonical order documented in `Application::update_simulation()` — `src/core/application.cpp`.
- [x] `m_atmosphere_on` plumbed to solar system renderer, sky background, and HUD each frame.
- [ ] Info Panel SolarSystem branch — *implemented in PR #13 (`src/ui/info_panel.cpp`); merged before this PR.*

**Known limitation (Option A):** When atmosphere is OFF, Solar System bodies become always-visible (no horizon cull). Stars, DSOs, and constellation lines still apply horizon cull. Full atmosphere-off for all renderers is a follow-up item.

### Checklist (original from Task 6.7)
- [x] Sun position correct (verify against known sunrise/sunset times) — PR #12
- [x] Moon position correct with phase and illumination — PR #12
- [x] All 7 planets visible at correct positions — PR #12
- [x] Planet colors distinguishable — PR #12
- [x] Moon phase visually indicated — PR #12
- [x] Twilight transitions visible (dawn/dusk at time acceleration) — PR #14
- [x] Day sky bright blue with Sun visible — PR #14
- [x] Moon glow affects sky brightness — PR #14
- [x] Atmosphere toggle ON/OFF works (A key + toolbar button) — this PR (Task 6.7)
- [x] Atmosphere OFF: pure black sky; Solar System bodies always visible (see limitation above) — this PR + PR #14
- [x] Solar system objects selectable with info panel — PR #13
- [x] HUD shows sky state — this PR (Task 6.7)
- [x] Planets move visibly at ×1000+ time speed — PR #12
- [ ] ≥ 60fps — *not measured in this environment; ephemeris computation is ~0.1ms/frame*
- [ ] No Vulkan validation errors — *not measurable without Vulkan GPU*
- [x] All previous features still work — maintained throughout Sprint 06

---

## Task Order

```
6.1 → 6.2 → 6.3 → 6.4 → 6.5 → 6.6 → 6.7
(sun) (moon) (planets) (sky state) (render SS) (twilight) (UI integration)
```

---

## Data Sources & References

| Data                 | Source                              |
|----------------------|-------------------------------------|
| Solar position       | Meeus "Astronomical Algorithms" Ch. 25 |
| Lunar position       | Meeus Ch. 47 (simplified ELP)       |
| Planetary elements   | Meeus Table 31.A (J2000 epoch)      |
| Planet magnitudes    | Meeus Ch. 41 + Harris/Miles 2007    |
| Moon illumination    | Meeus Ch. 48                        |
| Twilight model       | USNO definitions                    |

All algorithms from Meeus are well-established, public domain math.
No external data files needed — all coefficients are embedded as constexpr arrays.

---

## Performance Notes

Ephemeris computation for all bodies: ~0.1ms per frame (trivial).
The main cost is the same as before: star transform + rendering.
Solar system adds only 9 additional objects to render — negligible.

The twilight sky shader is slightly more complex but still a single fullscreen
fragment shader — no performance impact.

---

## Architecture Note

All solar system computation is in `parallax::astro::SolarSystem`.
This is a pure computation module — no rendering, no state.
Feed it a Julian Date, get back positions.

The renderer (`SolarSystemRenderer`) takes positions and draws them.
This separation means the same ephemeris code will be reused by the
imaging mode when it needs accurate Sun/Moon positions for sky brightness.
