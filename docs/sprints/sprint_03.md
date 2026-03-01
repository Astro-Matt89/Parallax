# Sprint 03 — Sky, Atmosphere & Full Catalog

**Prerequisite:** Sprint 02 complete (real starfield, camera, time, input)
**Goal:** Realistic sky background, atmospheric effects, full Hipparcos catalog, retro HUD overlay.
**Deliverable:** A visually rich planetarium with gradient sky, atmospheric dimming near horizon, ~118k stars, and on-screen information panels.

---

## Overview

Sprint 03 adds the layers that make Parallax feel like a real observing experience:
- The sky is no longer pure black — it has a realistic gradient
- Stars near the horizon are dimmed and reddened by the atmosphere
- The full Hipparcos catalog replaces the 21-star test set
- A retro HUD shows coordinates, time, FOV, and observer info

---

## Tasks

### Task 3.1 — Rendering: Sky Background

Procedural sky gradient rendered as a fullscreen pass before the starfield.

**Files:**
- `src/rendering/sky_background.hpp`, `src/rendering/sky_background.cpp`
- `shaders/sky_background.vert`
- `shaders/sky_background.frag`

**Interface:**
```cpp
namespace parallax::rendering
{
    struct SkyParams
    {
        f32 bortle_scale = 4.0f;        // 1-9
        f32 sun_altitude_deg = -18.0f;  // Below -18° = astronomical night
        f32 moon_altitude_deg = -90.0f; // Below horizon = no moon
        f32 moon_phase = 0.0f;          // 0 = new, 1 = full
    };

    class SkyBackground
    {
    public:
        void init(vulkan::Context& context, VkRenderPass render_pass,
                  VkExtent2D extent);
        void destroy();

        void update_params(const SkyParams& params,
                          const rendering::Camera& camera);

        void render(VkCommandBuffer cmd);

    private:
        // Vulkan resources: pipeline, descriptor set, uniform buffer
        // ...
    };
}
```

**Sky Model (Fragment Shader):**

The shader receives camera pointing + sky parameters via uniform buffer.

```glsl
// For each fragment, compute its Alt/Az based on camera pointing and screen position
// Then compute sky color based on altitude:

// Base night sky color (Bortle-dependent):
//   zenith:  (0.01, 0.01, 0.03) at Bortle 1 → (0.05, 0.04, 0.06) at Bortle 9
//   horizon: 2-5x brighter than zenith (light pollution glow)

// Gradient model:
//   sky_brightness = zenith_brightness × (1.0 + lp_factor × airmass(alt))
//   Where lp_factor scales with Bortle

// Color shift:
//   Zenith: deep blue-black
//   Horizon: warmer (light pollution has warm spectrum: sodium lights)
//   Blend: mix(zenith_color, horizon_color, smoothstep based on altitude)

// Twilight (sun_altitude > -18°):
//   Civil twilight (-6° to 0°): bright blue/orange
//   Nautical (-12° to -6°): deep blue
//   Astronomical (-18° to -12°): faint glow

// For Sprint 03, implement night sky + light pollution only.
// Twilight rendering is a bonus (can defer to Sprint 04).
```

**Implementation approach:**
- Fullscreen triangle (single triangle covering entire screen, more efficient than quad)
- Vertex shader: generate positions procedurally from `gl_VertexIndex` (0,1,2)
- Fragment shader: compute sky color per pixel
- Uniform buffer: camera pointing, FOV, sky params
- Render BEFORE starfield (stars are additive on top)

**Acceptance:**
- Dark sky with visible gradient from zenith to horizon
- Horizon visibly brighter than zenith
- Higher Bortle values → brighter, more orange horizon glow
- No seams or artifacts at edges

---

### Task 3.2 — Astro: Atmospheric Effects

Refraction and extinction applied to star positions and brightness.

**Files:** `src/astro/atmosphere.hpp`, `src/astro/atmosphere.cpp`, `tests/test_atmosphere.cpp`

**Interface:**
```cpp
namespace parallax::astro
{
    struct AtmosphereParams
    {
        f32 pressure_mbar = 1013.25f;
        f32 temperature_c = 15.0f;
        f32 extinction_coeff = 0.20f;   // mag/airmass (V band)
        f32 bortle_scale = 4.0f;
    };

    class Atmosphere
    {
    public:
        explicit Atmosphere(const AtmosphereParams& params = {});

        /// Atmospheric refraction: returns correction in radians (add to true altitude)
        [[nodiscard]] f64 refraction(f64 true_altitude_rad) const;

        /// Airmass at given true altitude
        [[nodiscard]] f64 airmass(f64 true_altitude_rad) const;

        /// Extinction in magnitudes at given altitude
        [[nodiscard]] f32 extinction_mag(f64 true_altitude_rad) const;

        /// Extinction as linear brightness factor (0..1) at given altitude
        [[nodiscard]] f32 extinction_factor(f64 true_altitude_rad) const;

        /// Sky surface brightness at zenith (mag/arcsec²)
        [[nodiscard]] f32 sky_brightness_zenith() const;

        /// Naked-eye limiting magnitude at given altitude
        [[nodiscard]] f32 limiting_magnitude(f64 true_altitude_rad) const;

        void set_params(const AtmosphereParams& params);

    private:
        AtmosphereParams m_params;
    };
}
```

**Formulas (from docs/architecture/atmosphere_model.md):**

Refraction (Bennett):
```
R' = 1.0 / tan(h + 7.31 / (h + 4.4))   [arcminutes, h in degrees]
R = R' × (P / 1010) × (283 / (273 + T))
Return R converted to radians.
For h < 0°: clamp to horizon value.
```

Airmass (Rozenberg):
```
X = 1.0 / (cos(z) + 0.025 × exp(-11 × cos(z)))
Where z = π/2 - altitude (zenith angle)
For alt < 0°: return large value (e.g., 40)
```

Extinction:
```
Δm = k × X   (magnitudes lost)
Linear factor = 10^(-0.4 × Δm)
```

**Tests:**
- Airmass at zenith ≈ 1.0
- Airmass at 30° altitude ≈ 2.0
- Airmass at 10° altitude ≈ 5.6
- Refraction at 45° ≈ 58" (arcseconds)
- Refraction at 0° (horizon) ≈ 34' (arcminutes)
- Extinction at zenith with k=0.2 ≈ 0.2 mag

**Acceptance:** All formulas produce values matching published references. Tests pass.

---

### Task 3.3 — Rendering: Apply Atmosphere to Starfield

Integrate atmospheric effects into the star rendering pipeline.

**Changes to:** `src/rendering/starfield.cpp` and Application frame loop

**Per-star processing (CPU side, updated pipeline):**
```
For each star:
  1. RA/Dec → Alt/Az (existing)
  2. Apply atmospheric refraction → apparent altitude
  3. Skip if apparent alt < -0.5° (allow slightly below horizon for refraction)
  4. Compute extinction factor at this altitude
  5. Adjusted brightness = base_brightness × extinction_factor
  6. Skip if adjusted brightness below threshold
  7. Project apparent Alt/Az to screen coords
  8. Add to GPU buffer with adjusted brightness
```

**Color reddening near horizon:**
Stars near the horizon lose blue light preferentially (Rayleigh scattering).
Simple model:
```
// In vertex shader or CPU side:
// At low altitude, shift B-V redder
effective_bv = original_bv + reddening(altitude)
reddening(alt) = 0.1 × (airmass - 1.0)   // Approximate
Clamp to valid B-V range [-0.4, 2.0]
```

**Acceptance:**
- Stars near horizon are noticeably dimmer
- Stars near horizon are subtly redder
- Very faint stars near horizon disappear (extinction exceeds visibility)
- Refraction slightly lifts stars near horizon
- Overall star count near horizon is lower than at zenith

---

### Task 3.4 — Catalog: Hipparcos Full Catalog

Load the complete Hipparcos catalog (~118,218 stars).

**Files:**
- `tools/prepare_hipparcos.py` — Python script to convert raw data
- Updated `src/catalog/catalog_loader.cpp`

**Data source:**
The Hipparcos main catalog is available from CDS (Centre de Données astronomiques de Strasbourg).

The Python preprocessing script should:
```python
# Input: hip_main.dat (fixed-width format) or hip2.dat
# Output: data/catalogs/hipparcos.csv
#
# Columns to extract:
#   HIP number (field H1)
#   RA in degrees (field H8)
#   Dec in degrees (field H9)
#   V magnitude (field H5)
#   B-V color index (field H37)
#
# Filter out:
#   Entries with no valid magnitude
#   Entries with no valid position
#
# Output format:
#   HIP,RA_deg,Dec_deg,Vmag,BV
```

**Loader update:**
The existing CSV loader should handle this format.
No changes needed if the columns match.

**Performance consideration:**
118k stars × coordinate transform per frame:
- ~118k × ~200ns = ~24ms → too slow for 60fps

**Optimization required:**
```
Option A: Transform only stars in FOV
  1. Rough prefilter: skip stars whose declination is clearly outside visible range
  2. Only transform stars that could be within FOV + margin

Option B: Spatial prefilter by hour angle range
  1. Compute RA range visible at current LST
  2. Only process stars in that RA band

Implement Option A for Sprint 03. More sophisticated spatial indexing in Sprint 04.
```

**Prefilter strategy:**
```cpp
// Rough visibility check (before full transform):
// A star at declination dec is visible if:
//   altitude_max = 90° - |latitude - dec|
//   If altitude_max < 0 → star never rises → skip

// Also skip if star is definitely below horizon:
//   hour_angle = LST - RA
//   cos(H_set) = -tan(lat) × tan(dec)
//   If |hour_angle| > H_set → star is set → skip
```

**Acceptance:**
- Full Hipparcos catalog loads (~118k stars)
- Rendering remains ≥ 60fps
- Dense starfields visible (Milky Way region should be noticeably denser)
- Log shows star count per frame (visible stars after culling)

---

### Task 3.5 — UI: Bitmap Font Renderer

GPU-rendered bitmap font for the retro HUD.

**Files:**
- `src/ui/font.hpp`, `src/ui/font.cpp`
- `shaders/text.vert`, `shaders/text.frag`
- `data/fonts/` — bitmap font atlas

**Approach:**
Use a simple monospace bitmap font atlas (8×16 pixels per character, ASCII 32-127).
This gives the retro terminal aesthetic.

**Font atlas:**
- 16×6 grid of characters = 128×96 pixel texture (or 256×256 with padding)
- Each character cell: 8×16 pixels
- White on transparent (tint with color in shader)
- Generate from a classic DOS font (IBM VGA 8×16, or similar public domain font)
- Store as raw pixel data or PNG in `data/fonts/cp437_8x16.png`

**Interface:**
```cpp
namespace parallax::ui
{
    class BitmapFont
    {
    public:
        void init(vulkan::Context& context, VkRenderPass render_pass,
                  const std::filesystem::path& font_atlas_path);
        void destroy();

        /// Queue text for rendering
        void draw_text(const std::string& text, f32 x, f32 y,
                      f32 scale = 1.0f, Vec3f color = {0.0f, 1.0f, 0.0f});

        /// Flush all queued text in one draw call
        void render(VkCommandBuffer cmd, VkExtent2D viewport_extent);

    private:
        struct TextVertex
        {
            Vec2f position;
            Vec2f texcoord;
            Vec3f color;
        };

        // Vulkan: pipeline, texture, sampler, vertex buffer
        // Batch of TextVertex quads for all queued text
    };
}
```

**Rendering approach:**
- Each character = 2 triangles (quad)
- All characters batched into one vertex buffer
- Single draw call per frame
- Alpha blending (not additive — text must be opaque)
- Pipeline: separate from starfield, rendered last (overlay)
- Vertex shader: screen-space positioning (pixel coordinates)
- Fragment shader: sample atlas texture, multiply by color, discard if alpha < 0.5

**Acceptance:**
- Green monospace text visible on screen
- Readable at 1× and 2× scale
- Classic terminal/CRT appearance
- Efficient: all text in single draw call

---

### Task 3.6 — UI: HUD Overlay

On-screen information panels using the bitmap font.

**Files:** `src/ui/hud.hpp`, `src/ui/hud.cpp`

**Interface:**
```cpp
namespace parallax::ui
{
    struct HudData
    {
        // Time
        f64 julian_date;
        f64 local_sidereal_time_rad;
        f64 utc_hours;

        // Camera
        f64 altitude_deg;
        f64 azimuth_deg;
        f64 fov_deg;
        f32 magnitude_limit;

        // Observer
        f64 latitude_deg;
        f64 longitude_deg;
        f32 bortle_scale;

        // Performance
        f32 fps;
        u32 visible_stars;
        u32 total_stars;

        // Simulation
        f64 time_scale;
    };

    class Hud
    {
    public:
        void init(vulkan::Context& context, VkRenderPass render_pass);
        void destroy();

        void update(const HudData& data);
        void render(VkCommandBuffer cmd, VkExtent2D viewport_extent);

    private:
        BitmapFont m_font;
        HudData m_data;
    };
}
```

**Layout:**
```
┌──────────────────────────────────────────────────────────┐
│ PARALLAX v0.1.0                              [top-left]  │
│ ──────────────────                                       │
│ UTC  2025-03-15 22:45:31                                 │
│ LST  14h 23m 07s                                         │
│ JD   2460749.448                                         │
│                                                          │
│                                                          │
│                                                          │
│                                                [top-right]│
│                                     ALT  +45° 12' 33"   │
│                                     AZ   180° 05' 21"   │
│                                     FOV  60.0°           │
│                                     MLIM 6.5             │
│                                                          │
│                                                          │
│                                                          │
│ [bottom-left]                                            │
│ LAT  +28° 45' 36" N                                     │
│ LON  -17° 53' 24" W                                     │
│ BORTLE 4                                                 │
│                                                          │
│ [bottom-right]                                           │
│                                     FPS  60 │ ★ 8432    │
│                                     TIME ×1.0            │
└──────────────────────────────────────────────────────────┘
```

**Color scheme (retro terminal):**
- Labels: dark green (#00AA00)
- Values: bright green (#00FF00)
- Title: bright green
- Separator: dark green dashes

**Formatting helpers needed:**
```cpp
// Format RA as HH:MM:SS
std::string format_ra(f64 ra_rad);
// Format Dec/Alt as ±DD° MM' SS"
std::string format_dms(f64 angle_rad);
// Format azimuth as DDD° MM' SS"
std::string format_az(f64 az_rad);
```

**Acceptance:**
- All four panels visible and readable
- Data updates in real-time
- No overlap with starfield (rendered on top)
- Retro green terminal aesthetic
- FPS and star count useful for performance monitoring

---

### Task 3.7 — Core: Simulation Clock Enhancements

Better time control for the simulation.

**Changes to:** Application, Input

**New key bindings:**
- `1` → real-time (×1)
- `2` → ×10
- `3` → ×100
- `4` → ×1000 (stars visibly rotate)
- `5` → ×10000
- `0` → pause (same as Space)
- `-` → reverse time (negative scale)
- `=` → reset to current real time
- `T` → toggle time display format (UTC / LST / JD)
- `B` → cycle Bortle scale (1→9→1)
- `H` → toggle HUD visibility

**Update to HudData:**
```cpp
// Show time scale in HUD:
//   TIME ×1.0  /  TIME ×100  /  TIME PAUSED  /  TIME ×-10
```

**Acceptance:**
- All key bindings work
- Time acceleration clearly visible (stars streaming across sky at ×1000+)
- Reverse time works
- HUD can be hidden/shown
- Bortle scale change immediately affects sky background brightness

---

### Task 3.8 — Application Integration

Wire sky background, atmosphere, full catalog, and HUD into the frame loop.

**Updated frame loop:**
```
1. Input new_frame + poll events
2. Process input:
   - Mouse drag → camera pan
   - Scroll → camera zoom
   - Keyboard → time controls, HUD toggle, Bortle cycle
3. Update simulation time (with time_scale)
4. Compute LST
5. Update atmosphere params
6. Update sky params (Bortle, sun altitude placeholder = -30°)
7. For each star in catalog:
   a. Rough visibility prefilter (dec + hour angle)
   b. RA/Dec → Alt/Az
   c. Apply refraction → apparent altitude
   d. Skip if below horizon
   e. Apply extinction → adjusted brightness
   f. Skip if too faint
   g. Apply reddening near horizon
   h. Project to screen
   i. Add to star buffer
8. Upload star buffer to GPU
9. Render:
   a. Begin render pass (clear)
   b. Sky background pass
   c. Starfield pass (additive over sky)
   d. HUD pass (alpha blend over everything)
   e. End render pass
10. Present
```

**Render pass update:**
Sprint 01-02 used a single clear + draw. Now we need proper pass ordering.
Options:
- A) Single render pass with subpasses (complex)
- B) Single render pass, draw order matters: sky → stars → HUD (simple, correct)

Use **Option B** for Sprint 03. Sky clears the screen, stars draw additive, HUD draws alpha-blended.
The sky background shader replaces the clear color.

**Acceptance (Sprint 03 Definition of Done):**
- [ ] Sky gradient visible (darker zenith, brighter horizon)
- [ ] Bortle scale affects sky brightness (cycle with B key)
- [ ] Stars near horizon are dimmer (atmospheric extinction)
- [ ] Stars near horizon are subtly redder (color reddening)
- [ ] Full Hipparcos catalog loaded (~118k stars)
- [ ] Dense star regions visible (Milky Way band denser)
- [ ] Performance ≥ 60fps with full catalog
- [ ] HUD shows time, coordinates, FOV, magnitude limit, FPS, star count
- [ ] HUD has retro green terminal aesthetic
- [ ] Time acceleration works (×1, ×10, ×100, ×1000, ×10000)
- [ ] Reverse time works
- [ ] HUD can be toggled on/off with H
- [ ] No Vulkan validation errors

---

## Task Order

```
3.1 → 3.2 → 3.3 → 3.4 → 3.5 → 3.6 → 3.7 → 3.8
(sky) (atmo) (apply) (catalog) (font) (hud) (clock) (integrate)
```

---

## Copilot Prompt — Sprint 03 Kickoff

```
Read these files before starting:
- CLAUDE.md
- docs/sprints/sprint_03.md
- docs/architecture/rendering_pipeline.md
- docs/architecture/atmosphere_model.md
- docs/architecture/catalog_system.md

Sprint 02 is complete. We have a working planetarium with 21 bright stars,
coordinate transforms, camera control, and time simulation.

Start Sprint 03: Sky, Atmosphere & Full Catalog.
Begin with Task 3.1 (Sky Background).
One task at a time. Wait for my confirmation before moving to the next task.
```

---

## Notes

### Font Atlas Generation
If creating the bitmap font atlas is complex, a simpler alternative for Sprint 03:
- Use stb_truetype to render a monospace font (like Consolas or Liberation Mono) to a texture at init time
- Or embed a hardcoded 8×16 font as a C array (many public domain CP437 fonts available as header files)
- The visual result should still look retro — monospace, pixel-sharp, green on dark

### Hipparcos Data
If downloading the full Hipparcos catalog is blocked, the bright_stars.csv from Sprint 02
still works. The rendering pipeline is the same — just fewer stars.
A fallback option: the Yale Bright Star Catalog (~9,096 stars, all visible to naked eye)
is simpler to obtain and sufficient for Sprint 03 visual testing.
