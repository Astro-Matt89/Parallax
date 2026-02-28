# Sprint 02 — Planetarium Core

**Prerequisite:** Sprint 01 complete (Vulkan pipeline, window, test render)
**Goal:** Render a real starfield from astronomical catalog data with coordinate transforms.
**Deliverable:** A window showing real stars in correct positions, with pan/zoom, time-based rotation.

---

## Overview

This sprint transforms the Vulkan test bed into a functioning planetarium.
At the end of Sprint 02, the player sees the real night sky from a given location and time.

Key systems introduced:
- Astronomical time (Julian Date, sidereal time)
- Coordinate transforms (J2000 RA/Dec → horizontal Alt/Az → screen)
- Star catalog loading (Hipparcos bright stars)
- Instanced star rendering (magnitude → size + brightness)
- Camera system (pointing direction, field of view)
- Basic input (mouse pan, scroll zoom)

---

## Tasks

### Task 2.1 — Core: Types and Math Utilities

Common type aliases and math helpers used across all modules.

**File:** `src/core/types.hpp`

```cpp
#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdint>

namespace parallax
{
    // Precision aliases
    using f32 = float;
    using f64 = double;
    using u8  = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    using i32 = int32_t;
    using i64 = int64_t;

    // Vector types (double precision for astronomy)
    using Vec2d = glm::dvec2;
    using Vec3d = glm::dvec3;
    using Vec4d = glm::dvec4;
    using Mat3d = glm::dmat3;
    using Mat4d = glm::dmat4;

    // Vector types (float for rendering)
    using Vec2f = glm::vec2;
    using Vec3f = glm::vec3;
    using Vec4f = glm::vec4;
    using Mat4f = glm::mat4;

    // Astronomical constants
    namespace astro_constants
    {
        constexpr f64 kPi          = glm::pi<f64>();
        constexpr f64 kTwoPi       = 2.0 * kPi;
        constexpr f64 kHalfPi      = kPi / 2.0;
        constexpr f64 kDegToRad    = kPi / 180.0;
        constexpr f64 kRadToDeg    = 180.0 / kPi;
        constexpr f64 kHourToRad   = kPi / 12.0;
        constexpr f64 kRadToHour   = 12.0 / kPi;
        constexpr f64 kArcSecToRad = kPi / (180.0 * 3600.0);
        constexpr f64 kJ2000       = 2451545.0;  // Julian Date of J2000.0 epoch
    }
}
```

**Acceptance:** Compiles, included by subsequent modules.

---

### Task 2.2 — Astro: Time System

Julian Date computation and sidereal time.

**Files:** `src/astro/time_system.hpp`, `src/astro/time_system.cpp`

**Interface:**
```cpp
namespace parallax::astro
{
    struct DateTime
    {
        i32 year;
        i32 month;
        i32 day;
        i32 hour;
        i32 minute;
        f64 second;
    };

    class TimeSystem
    {
    public:
        /// Convert civil date/time (UTC) to Julian Date
        [[nodiscard]] static f64 to_julian_date(const DateTime& dt);

        /// Convert Julian Date back to civil date/time
        [[nodiscard]] static DateTime from_julian_date(f64 jd);

        /// Julian centuries since J2000.0
        [[nodiscard]] static f64 julian_centuries(f64 jd);

        /// Greenwich Mean Sidereal Time (radians)
        [[nodiscard]] static f64 gmst(f64 jd);

        /// Local Mean Sidereal Time (radians)
        [[nodiscard]] static f64 lmst(f64 jd, f64 longitude_rad);

        /// Get current system time as Julian Date
        [[nodiscard]] static f64 now_as_jd();
    };
}
```

**Implementation details:**
- Julian Date: Meeus algorithm (Astronomical Algorithms, Ch. 7)
- GMST: IAU 1982 formula (accurate to ~0.1 second)
  ```
  GMST = 280.46061837 + 360.98564736629 × (JD − 2451545.0)
         + 0.000387933 × T² − T³/38710000
  Where T = Julian centuries from J2000.0
  Result in degrees, normalize to [0, 360), convert to radians.
  ```
- LMST = GMST + observer longitude

**Tests:** `tests/test_time_system.cpp`
- J2000.0 epoch (2000-01-01 12:00 UTC) → JD = 2451545.0
- Known date verification (e.g., 2024-06-15 22:30:00 UTC)
- Round-trip: DateTime → JD → DateTime must match
- GMST at J2000.0 ≈ 280.46° (≈ 18h 41m)

**Acceptance:** All tests pass. Julian Date and sidereal time match published reference values.

---

### Task 2.3 — Astro: Coordinate Transforms

Equatorial (RA/Dec) to Horizontal (Alt/Az) and to screen projection.

**Files:** `src/astro/coordinates.hpp`, `src/astro/coordinates.cpp`

**Interface:**
```cpp
namespace parallax::astro
{
    struct EquatorialCoord
    {
        f64 ra;     // Right ascension (radians, 0..2π)
        f64 dec;    // Declination (radians, -π/2..+π/2)
    };

    struct HorizontalCoord
    {
        f64 alt;    // Altitude (radians, -π/2..+π/2, negative = below horizon)
        f64 az;     // Azimuth (radians, 0..2π, 0=North, π/2=East)
    };

    struct ObserverLocation
    {
        f64 latitude_rad;
        f64 longitude_rad;
    };

    class Coordinates
    {
    public:
        /// Equatorial (RA/Dec J2000) → Horizontal (Alt/Az)
        [[nodiscard]] static HorizontalCoord equatorial_to_horizontal(
            const EquatorialCoord& eq,
            const ObserverLocation& observer,
            f64 local_sidereal_time_rad
        );

        /// Horizontal → Equatorial (inverse)
        [[nodiscard]] static EquatorialCoord horizontal_to_equatorial(
            const HorizontalCoord& hz,
            const ObserverLocation& observer,
            f64 local_sidereal_time_rad
        );

        /// Horizontal (Alt/Az) → Stereographic screen projection
        /// Returns normalized coordinates: (0,0) = center of view, range depends on FOV
        /// pointing_alt/az = where the camera is looking
        [[nodiscard]] static std::optional<Vec2f> horizontal_to_screen(
            const HorizontalCoord& star,
            const HorizontalCoord& pointing,
            f64 fov_rad
        );
    };
}
```

**Implementation details:**

Equatorial → Horizontal:
```
Hour angle: H = LST - RA

sin(alt) = sin(dec) × sin(lat) + cos(dec) × cos(lat) × cos(H)
cos(az) × cos(alt) = sin(dec) × cos(lat) - cos(dec) × sin(lat) × cos(H)  [NORTH-BASED]
sin(az) × cos(alt) = -cos(dec) × sin(H)

az = atan2(sin(az)×cos(alt), cos(az)×cos(alt))
Normalize az to [0, 2π)
```

Stereographic projection (for screen mapping):
```
Given star and pointing direction in Alt/Az:
1. Convert both to Cartesian unit vectors
2. Compute angular separation
3. If separation > FOV/2: return std::nullopt (off screen)
4. Project using stereographic:
   dx = cos(alt_star) × sin(Δaz)
   dy = sin(alt_star) × cos(alt_center) - cos(alt_star) × sin(alt_center) × cos(Δaz)
   Scale by FOV to get normalized screen coords
```

**Tests:** `tests/test_coordinates.cpp`
- Polaris (RA≈02h31m, Dec≈+89°15') from lat 45°N at midnight → near zenith
- Star at RA = LST, Dec = lat → altitude ≈ 90° (transit at zenith)
- Known star positions verified against Stellarium or USNO

**Acceptance:** Coordinate transforms match reference values within 1 arcsecond.

---

### Task 2.4 — Catalog: Star Data and Loader

Load a real star catalog from CSV/text into memory.

**Files:**
- `src/catalog/star_entry.hpp`
- `src/catalog/catalog_loader.hpp`, `src/catalog/catalog_loader.cpp`

**Star Entry (runtime, expanded for ease of use):**
```cpp
namespace parallax::catalog
{
    struct StarEntry
    {
        f64 ra;             // Right ascension (radians)
        f64 dec;            // Declination (radians)
        f32 mag_v;          // Visual magnitude
        f32 color_bv;       // B-V color index
        u32 catalog_id;     // Source catalog ID (e.g., HIP number)
    };
}
```

**Catalog Loader:**
```cpp
namespace parallax::catalog
{
    class CatalogLoader
    {
    public:
        /// Load stars from Hipparcos CSV file
        /// Expected columns: HIP, RA(deg), Dec(deg), Vmag, B-V
        [[nodiscard]] static std::optional<std::vector<StarEntry>>
            load_hipparcos_csv(const std::filesystem::path& path);

        /// Load stars from a simple text format (for testing)
        /// Each line: RA_deg Dec_deg Vmag B-V
        [[nodiscard]] static std::optional<std::vector<StarEntry>>
            load_simple_text(const std::filesystem::path& path);
    };
}
```

**Data source for Phase 1:**
Use the Hipparcos main catalog (`hip_main.dat` or a preprocessed CSV).
Available from: https://cdsarc.cds.unistra.fr/viz-bin/cat/I/239

For Sprint 02, we need a CSV with columns:
```
HIP,RA_deg,Dec_deg,Vmag,BV
1,0.0005,1.0893,9.10,0.482
2,0.0024,-19.4989,9.27,0.999
...
```

**Preprocessing:** Create a simple Python script `tools/prepare_hipparcos.py`
that converts raw Hipparcos data to this clean CSV format.

If full Hipparcos is not available yet, create a **test catalog** with:
- 20-30 bright stars (Sirius, Vega, Polaris, Betelgeuse, etc.)
- Correct RA/Dec (J2000) and magnitudes
- This allows rendering verification before full catalog integration

**Acceptance:**
- Loads CSV file, parses all entries
- Logs: "Loaded {N} stars from {filename}"
- Test catalog of bright stars loads correctly
- Magnitudes and positions verified against SIMBAD

---

### Task 2.5 — Rendering: Camera System

Camera that defines where the observer is looking and the field of view.

**Files:** `src/rendering/camera.hpp`, `src/rendering/camera.cpp`

**Interface:**
```cpp
namespace parallax::rendering
{
    class Camera
    {
    public:
        Camera();

        /// Set pointing direction (Alt/Az)
        void set_pointing(f64 altitude_rad, f64 azimuth_rad);

        /// Set field of view
        void set_fov(f64 fov_deg);

        /// Adjust pointing by delta (for mouse drag)
        void pan(f64 delta_az_rad, f64 delta_alt_rad);

        /// Zoom in/out (adjust FOV)
        void zoom(f64 factor);

        [[nodiscard]] astro::HorizontalCoord get_pointing() const;
        [[nodiscard]] f64 get_fov_rad() const;
        [[nodiscard]] f64 get_fov_deg() const;

        /// Get limiting magnitude for current FOV (naked eye approximation)
        [[nodiscard]] f32 get_magnitude_limit() const;

    private:
        f64 m_altitude = glm::radians(45.0);   // Looking 45° up
        f64 m_azimuth = 0.0;                    // Looking North
        f64 m_fov = glm::radians(60.0);         // 60° FOV (naked eye)

        static constexpr f64 kMinFov = glm::radians(0.5);    // Max zoom in
        static constexpr f64 kMaxFov = glm::radians(120.0);  // Max zoom out
    };
}
```

**Magnitude limit heuristic:**
```
At 60° FOV (naked eye): mag limit ≈ 6.5
At 5° FOV (binoculars): mag limit ≈ 10
At 0.5° FOV (telescope): mag limit ≈ 14

Simple formula: mag_limit = 6.5 + 5 × log10(60.0 / fov_degrees)
Clamped to catalog maximum.
```

**Acceptance:** Camera stores pointing and FOV. Pan/zoom works with clamping.

---

### Task 2.6 — Rendering: Star Renderer

Replace the single test point with an instanced starfield.

**Files:**
- `src/rendering/starfield.hpp`, `src/rendering/starfield.cpp`
- `shaders/starfield.vert` (replaces test_star.vert)
- `shaders/starfield.frag` (replaces test_star.frag)

**GPU Data Structure:**
```cpp
struct StarVertex  // Per-instance, uploaded to GPU each frame
{
    f32 screen_x;      // Normalized device coords [-1, 1]
    f32 screen_y;
    f32 brightness;    // Linear brightness (after magnitude conversion)
    f32 color_packed;  // Encoded B-V → RGB
};
```

**Star Rendering Pipeline:**
1. CPU: Query catalog → get stars in FOV (by magnitude limit)
2. CPU: For each star:
   - RA/Dec → Alt/Az (using current LST and observer location)
   - Alt/Az → screen coords (stereographic projection)
   - Magnitude → brightness: `brightness = pow(10.0, -0.4 * (mag - mag_zero))`
   - B-V → color: lookup table
3. CPU: Upload `StarVertex` array to GPU storage buffer
4. GPU: Vertex shader reads from storage buffer, sets `gl_Position` and `gl_PointSize`
5. GPU: Fragment shader applies color and circular point shape

**Vertex Shader (starfield.vert):**
```glsl
#version 450

layout(set = 0, binding = 0) readonly buffer StarBuffer {
    vec4 stars[];  // xy = position, z = brightness, w = color
};

layout(push_constant) uniform PushConstants {
    float point_size_scale;
    float brightness_scale;
};

layout(location = 0) out float v_brightness;
layout(location = 1) out vec3 v_color;

// B-V to RGB conversion (simplified Planck approximation)
vec3 bv_to_rgb(float bv) {
    float t = 4600.0 / (1.7 * bv + 1.8) + 4600.0 / (0.92 * bv + 1.7);
    t = clamp(t, 1000.0, 40000.0);

    // Approximate blackbody RGB
    float x = t / 1000.0;
    float r, g, b;

    if (t < 6600.0) {
        r = 1.0;
        g = clamp(0.39 * log(x) - 0.634, 0.0, 1.0);
        b = clamp(0.543 * log(x - 1.0) - 1.185, 0.0, 1.0);
    } else {
        r = clamp(1.269 * pow(x - 6.0, -0.1332), 0.0, 1.0);
        g = clamp(1.144 * pow(x - 6.0, -0.0755), 0.0, 1.0);
        b = 1.0;
    }
    return vec3(r, g, b);
}

void main() {
    vec4 star = stars[gl_InstanceIndex];

    gl_Position = vec4(star.xy, 0.0, 1.0);

    float brightness = star.z * brightness_scale;
    float size = max(1.0, point_size_scale * sqrt(brightness));
    gl_PointSize = min(size, 8.0);

    v_brightness = clamp(brightness, 0.0, 1.0);
    v_color = bv_to_rgb(star.w);
}
```

**Fragment Shader (starfield.frag):**
```glsl
#version 450

layout(location = 0) in float v_brightness;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 frag_color;

void main() {
    // Circular point: discard corners for round stars
    vec2 coord = gl_PointCoord * 2.0 - 1.0;
    float dist = dot(coord, coord);
    if (dist > 1.0) discard;

    // Soft edge falloff
    float alpha = v_brightness * (1.0 - dist * dist);

    frag_color = vec4(v_color * alpha, alpha);
}
```

**Pipeline changes:**
- Topology: `VK_PRIMITIVE_TOPOLOGY_POINT_LIST`
- Blending: **additive** (`srcAlpha + one`)
- No vertex input (all data from storage buffer)
- Push constants for rendering parameters
- Draw call: `vkCmdDraw(1, star_count, 0, 0)` (instanced)

**Acceptance:**
- Real star positions visible
- Bright stars (Sirius, Vega) are larger and brighter
- Star colors visible (blue Rigel, red Betelgeuse)
- Smooth rendering, no flickering

---

### Task 2.7 — Core: Input Handler

Mouse and keyboard input for camera control.

**Files:** `src/core/input.hpp`, `src/core/input.cpp`

**Interface:**
```cpp
namespace parallax::core
{
    class Input
    {
    public:
        /// Call once per frame with SDL events
        void process_event(const SDL_Event& event);

        /// Reset per-frame state (call at start of frame)
        void new_frame();

        // Mouse state
        [[nodiscard]] bool is_mouse_dragging() const;
        [[nodiscard]] Vec2f get_mouse_drag_delta() const;
        [[nodiscard]] f32 get_scroll_delta() const;

        // Key state
        [[nodiscard]] bool is_key_pressed(SDL_Scancode key) const;
        [[nodiscard]] bool is_key_held(SDL_Scancode key) const;

    private:
        Vec2f m_mouse_drag_delta = {0.0f, 0.0f};
        f32 m_scroll_delta = 0.0f;
        bool m_mouse_dragging = false;
        bool m_left_button_down = false;
        Vec2f m_last_mouse_pos = {0.0f, 0.0f};

        std::unordered_set<SDL_Scancode> m_keys_pressed;  // This frame only
        std::unordered_set<SDL_Scancode> m_keys_held;     // While held
    };
}
```

**Mappings (Sprint 02):**
- Left mouse drag → pan camera (az/alt)
- Scroll wheel → zoom (change FOV)
- `Space` → pause/resume time flow
- `R` → reset camera to default (North, 45° up, 60° FOV)
- `Escape` → quit

**Acceptance:** Mouse drag pans sky smoothly. Scroll zooms in/out.

---

### Task 2.8 — Application Integration

Wire all new systems into the main loop.

**Changes to:** `src/core/application.hpp/cpp`

**New members:**
```cpp
// In Application class
std::unique_ptr<astro::TimeSystem> m_time_system;  // (static class, no instance needed)
std::unique_ptr<rendering::Camera> m_camera;
std::unique_ptr<rendering::Starfield> m_starfield;
std::unique_ptr<core::Input> m_input;

// Simulation state
f64 m_julian_date;              // Current simulation time
f64 m_time_scale = 1.0;        // 1.0 = real-time, 0.0 = paused
astro::ObserverLocation m_observer;

// Default observer: a good dark sky site
// Example: McDonald Observatory, Texas (lat 30.67°N, lon -104.02°W)
// Or: La Palma, Canary Islands (lat 28.76°N, lon -17.89°W)
```

**Updated frame loop:**
```
1. Input::new_frame()
2. Window::poll_events() → feed events to Input
3. Process input:
   - Mouse drag → Camera::pan()
   - Scroll → Camera::zoom()
   - Space → toggle time_scale (0.0 / 1.0)
   - Escape → close
4. Update simulation time:
   - m_julian_date += delta_time * m_time_scale / 86400.0
   (delta_time in seconds, JD is in days)
5. Compute LST:
   - lst = TimeSystem::lmst(m_julian_date, observer.longitude)
6. For each star in catalog:
   - eq_to_horizontal(star, observer, lst)
   - Skip if alt < 0 (below horizon)
   - horizontal_to_screen(star, camera.pointing, camera.fov)
   - Skip if off screen
   - Compute brightness and color
   - Add to StarVertex buffer
7. Upload buffer to GPU
8. Render: clear → draw starfield → present
```

**Acceptance (Sprint 02 Definition of Done):**
- [ ] Real stars visible in correct positions
- [ ] Bright stars (mag < 2) clearly brighter and larger
- [ ] Star colors match spectral type (Betelgeuse reddish, Rigel bluish)
- [ ] Mouse drag pans the sky smoothly
- [ ] Scroll wheel zooms in/out
- [ ] Stars rotate with time (sidereal motion visible when time flows)
- [ ] Space bar pauses/resumes time
- [ ] Stars below horizon not rendered
- [ ] No Vulkan validation errors
- [ ] Stable 60fps with full bright star catalog

---

## Test Catalog (Bright Stars)

If full Hipparcos is not yet available, use this embedded test data.
Create as `data/catalogs/bright_stars.csv`:

```csv
Name,RA_deg,Dec_deg,Vmag,BV
Sirius,101.287,-16.716,-1.46,0.009
Canopus,95.988,-52.696,-0.74,0.164
Arcturus,213.915,19.182,-0.05,1.239
Vega,279.235,38.784,0.03,0.000
Capella,79.172,45.998,0.08,0.795
Rigel,78.634,-8.202,0.13,-0.03
Procyon,114.826,5.225,0.34,0.432
Betelgeuse,88.793,7.407,0.42,1.850
Achernar,24.429,-57.237,0.46,-0.158
Hadar,210.956,-60.373,0.61,-0.231
Altair,297.696,8.868,0.76,0.221
Acrux,186.650,-63.099,0.76,-0.264
Aldebaran,68.980,16.510,0.86,1.538
Antares,247.352,-26.432,0.96,1.830
Spica,201.298,-11.161,0.97,-0.235
Pollux,116.329,28.026,1.14,0.991
Fomalhaut,344.413,-29.622,1.16,0.145
Deneb,310.358,45.280,1.25,0.092
Mimosa,191.930,-59.689,1.25,-0.238
Regulus,152.093,11.967,1.35,-0.114
Polaris,37.954,89.264,1.98,0.636
```

This gives 21 bright reference stars for visual verification.

---

## Task Order

```
2.1 → 2.2 → 2.3 → 2.4 → 2.5 → 2.6 → 2.7 → 2.8
(types) (time) (coords) (catalog) (camera) (stars) (input) (integrate)
```

---

## Copilot Prompt — Sprint 02 Kickoff

```
Read these files before starting:
- CLAUDE.md
- docs/sprints/sprint_02.md
- docs/architecture/overview.md
- docs/architecture/rendering_pipeline.md
- docs/architecture/catalog_system.md

Sprint 01 is complete. The Vulkan pipeline, window, and test render are working.

Start Sprint 02: Planetarium Core.
Begin with Task 2.1 (Types and Math Utilities).
Create src/core/types.hpp following the interface in sprint_02.md.
One task at a time. Wait for my confirmation before moving to the next task.
```
