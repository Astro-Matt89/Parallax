# PARALLAX — Project Context for AI Assistants

> Ground-Based Astronomical Observatory Simulator
> Language: C++20 | Renderer: Vulkan | Platform: Windows + Linux

---

## 1. Project Identity

**Parallax** is a ground-based astronomical observatory simulator.
The player never leaves Earth. The universe is explored exclusively through instruments.

Core pillars:
- Scientific realism over spectacle
- Instruments as primary gameplay interface
- Hybrid universe: real astronomical catalogs + procedural generation
- Retro scientific terminal aesthetic with cosmic depth
- Discovery-driven progression

Comparable to: SpaceEngine constrained to terrestrial observation.
Not a space shooter. Not arcade sci-fi. Observational science.

---

## 2. Technology Stack

| Component          | Choice                        | Notes                              |
|--------------------|-------------------------------|------------------------------------|
| Language           | C++20                         | GCC 12+ / MSVC 17.4+ / Clang 15+  |
| Build System       | CMake 3.25+                   |                                    |
| Package Manager    | vcpkg (manifest mode)         |                                    |
| Rendering API      | Vulkan 1.3                    | Full control, compute shaders      |
| Window / Input     | SDL2                          | Vulkan surface via SDL_Vulkan      |
| Math               | GLM                           | Header-only, GLSL-compatible       |
| Logging            | spdlog                        | Async, fast, header-only           |
| Shader Language    | GLSL → SPIR-V                 | Compiled offline via glslangValidator |
| Catalog Format     | Custom binary + spatial index | Memory-mapped, HEALPix/Octree     |

### Explicitly NOT in scope (for now)
- ImGui (may add later for dev tools)
- stb_image (evaluate when needed)
- Eigen (GLM sufficient)

---

## 3. C++ Coding Standard

### 3.1 Naming Conventions

| Element              | Convention              | Example                           |
|----------------------|-------------------------|-----------------------------------|
| Namespaces           | `snake_case`            | `parallax::rendering`             |
| Classes / Structs    | `PascalCase`            | `StarRenderer`, `CatalogEntry`    |
| Functions / Methods  | `snake_case`            | `render_frame()`, `load_catalog()`|
| Local variables      | `snake_case`            | `star_count`, `field_of_view`     |
| Member variables     | `m_` prefix             | `m_aperture`, `m_focal_length`    |
| Static members       | `s_` prefix             | `s_instance_count`                |
| Constants            | `k` + PascalCase        | `kMaxMagnitude`, `kDefaultFov`    |
| Enum types           | `PascalCase`            | `SensorType`                      |
| Enum values          | `PascalCase`            | `SensorType::Monochrome`          |
| Macros (avoid)       | `PLX_UPPER_SNAKE`       | `PLX_ASSERT`, `PLX_VERSION`       |
| Template params      | `PascalCase`            | `template<typename CoordSystem>`  |
| File names           | `snake_case`            | `star_renderer.hpp`, `.cpp`       |

### 3.2 File Organization

```
header:  .hpp
source:  .cpp
inline:  .inl  (heavy template implementations)
shader:  .vert / .frag / .comp
```

Header guard: `#pragma once` (all compilers in scope support it).

### 3.3 Include Order

```cpp
// 1. Corresponding header
#include "star_renderer.hpp"

// 2. Project headers
#include "core/logger.hpp"
#include "catalog/star_entry.hpp"

// 3. Third-party
#include <glm/glm.hpp>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

// 4. Standard library
#include <vector>
#include <memory>
#include <cstdint>
```

### 3.4 Modern C++20 Practices

**Use:**
- `std::unique_ptr` / `std::shared_ptr` for ownership
- `std::span` for non-owning array views
- `std::string_view` for non-owning string references
- `std::optional` for nullable returns
- `std::expected` (or custom Result<T,E>) for error handling
- `std::format` for string formatting
- Designated initializers for config structs
- Concepts for template constraints
- `[[nodiscard]]` on functions with important return values
- Structured bindings

**Avoid:**
- Raw `new` / `delete`
- C-style casts (use `static_cast`, `reinterpret_cast`)
- Macros for constants or functions
- `using namespace` in headers
- Global mutable state (use dependency injection)
- Exceptions for control flow (reserve for truly exceptional cases)

### 3.5 Error Handling Strategy

```
Vulkan calls    → check VkResult, log + abort on failure
Resource loading → return std::optional or Result<T, Error>
Assertions      → PLX_ASSERT(condition) in debug builds
Logging         → spdlog with levels: trace/debug/info/warn/error/critical
```

### 3.6 Formatting

- Indentation: 4 spaces (no tabs)
- Brace style: Allman (opening brace on new line)
- Max line length: 120 characters
- `clang-format` config provided in `.clang-format`

---

## 4. Project Structure

```
parallax/
├── CLAUDE.md                    # THIS FILE — AI context
├── CMakeLists.txt               # Root CMake
├── vcpkg.json                   # vcpkg manifest
├── .clang-format                # Code formatting rules
│
├── src/
│   ├── main.cpp                 # Entry point
│   │
│   ├── core/                    # Foundation layer
│   │   ├── application.hpp/cpp  # Main loop, lifecycle
│   │   ├── window.hpp/cpp       # SDL2 + Vulkan surface
│   │   ├── logger.hpp/cpp       # spdlog wrapper
│   │   ├── timer.hpp/cpp        # High-res timing
│   │   ├── config.hpp/cpp       # Runtime configuration
│   │   └── types.hpp            # Common type aliases
│   │
│   ├── vulkan/                  # Vulkan abstraction layer
│   │   ├── context.hpp/cpp      # Instance, device, queues
│   │   ├── swapchain.hpp/cpp    # Swapchain management
│   │   ├── pipeline.hpp/cpp     # Graphics/compute pipelines
│   │   ├── buffer.hpp/cpp       # VkBuffer wrapper
│   │   ├── image.hpp/cpp        # VkImage wrapper
│   │   ├── descriptor.hpp/cpp   # Descriptor sets/pools
│   │   ├── command.hpp/cpp      # Command buffers
│   │   ├── shader.hpp/cpp       # SPIR-V shader loading
│   │   └── sync.hpp/cpp         # Fences, semaphores
│   │
│   ├── rendering/               # High-level rendering
│   │   ├── renderer.hpp/cpp     # Frame orchestration
│   │   ├── starfield.hpp/cpp    # Star rendering pipeline
│   │   ├── sky_background.hpp/cpp
│   │   └── post_process.hpp/cpp # Bloom, tone mapping
│   │
│   ├── catalog/                 # Astronomical data
│   │   ├── star_entry.hpp       # Star data structure
│   │   ├── catalog_loader.hpp/cpp
│   │   ├── spatial_index.hpp/cpp # HEALPix / Octree
│   │   └── magnitude_filter.hpp/cpp
│   │
│   ├── astro/                   # Astronomical calculations
│   │   ├── coordinates.hpp/cpp  # RA/Dec, Alt/Az, transforms
│   │   ├── time_system.hpp/cpp  # JD, sidereal time
│   │   ├── precession.hpp/cpp   # Nutation, aberration
│   │   └── atmosphere.hpp/cpp   # Refraction, extinction
│   │
│   ├── observatory/             # Simulation layer
│   │   ├── telescope.hpp/cpp    # Optics model
│   │   ├── sensor.hpp/cpp       # CCD/CMOS simulation
│   │   ├── site.hpp/cpp         # Location, weather
│   │   └── session.hpp/cpp      # Observing session state
│   │
│   └── ui/                      # User interface
│       ├── hud.hpp/cpp          # Retro console overlay
│       └── panel.hpp/cpp        # UI panel system
│
├── shaders/                     # GLSL sources
│   ├── starfield.vert
│   ├── starfield.frag
│   ├── sky_background.frag
│   ├── bloom.comp
│   └── compile_shaders.sh       # SPIR-V compilation script
│
├── data/                        # Runtime data
│   ├── catalogs/                # Binary catalog files
│   └── config/                  # Default configs
│
├── tools/                       # Offline utilities
│   ├── catalog_converter/       # Raw → binary converter
│   └── shader_compiler/         # Batch SPIR-V compilation
│
├── tests/                       # Unit tests
│   ├── test_coordinates.cpp
│   ├── test_spatial_index.cpp
│   └── CMakeLists.txt
│
└── docs/                        # Project documentation
    ├── architecture/            # Technical design docs
    │   ├── overview.md
    │   ├── rendering_pipeline.md
    │   ├── catalog_system.md
    │   └── atmosphere_model.md
    └── sprints/                 # Sprint briefs for AI
        └── sprint_01.md
```

---

## 5. Architecture Overview

### 5.1 Layer Model

```
┌─────────────────────────────────────────────────┐
│                   Application                    │
│              (main loop, lifecycle)              │
├─────────────────────────────────────────────────┤
│        Observatory          │       UI           │
│   (telescope, sensor,       │  (HUD, panels,     │
│    site, session)           │   console)          │
├─────────────────────────────────────────────────┤
│      Rendering              │     Astro           │
│  (starfield, sky,           │  (coords, time,     │
│   post-process)             │   atmosphere)        │
├─────────────────────────────────────────────────┤
│              Vulkan Abstraction Layer             │
│   (context, swapchain, pipeline, buffers, sync)  │
├─────────────────────────────────────────────────┤
│         Catalog             │      Core            │
│  (loader, spatial index,    │  (window, logger,    │
│   magnitude filter)         │   timer, config)     │
└─────────────────────────────────────────────────┘
```

### 5.2 Dependency Rules

- **Core** depends on nothing (except STL + spdlog)
- **Vulkan** depends on Core
- **Catalog** depends on Core (no rendering dependency)
- **Astro** depends on Core + GLM (pure math, no rendering)
- **Rendering** depends on Vulkan + Catalog + Astro
- **Observatory** depends on Astro + Catalog
- **UI** depends on Rendering + Observatory
- **Application** depends on everything

No circular dependencies. Each module testable in isolation.

### 5.4 Parallax Vision & Project Modes

**Parallax is Space Engine without a spacecraft.**

The player is a ground-based astronomer with real and futuristic instruments.
The gameplay is discovery-driven: point instruments, collect data, analyze it,
unlock progressively deeper knowledge of celestial objects. Gameplay emerges
from physics and real observation techniques.

The universe is **hybrid**: real astronomical catalogs + procedural generation
of everything else. Sub-universes make every catalog object itself a world:
M31 contains billions of stars, Sirius contains planets, the Sun contains asteroids.

**Skychart Mode** (Phase 1 — COMPLETE, will be refactored in Sprint 08):
Planetarium/encyclopedia of what is KNOWN to the player.
- Historical catalog objects (Hipparcos, Tycho-2, Messier) always visible
  with L1-L3 properties (position, mag, color, basic classification)
- Procedural objects visible ONLY after player discovers them
- Deep properties (L4+) of historical objects must be measured by the player
- Toggle atmosphere on/off for observation planning
- Think: your personal research database, not Stellarium

**Imaging Mode** (Phase 2 — Sprint 09+):
What the telescope/sensor actually captures. The core of Parallax.
- Full atmospheric model: refraction, extinction, seeing, scintillation
- Optical simulation: telescope PSF, diffraction, aberrations, field curvature
- Sensor simulation: CCD/CMOS, noise, dark current, gain, bit depth
- Astrophotography pipeline: exposure, stacking, calibration, stretching
- Real integration time, realistic SNR progression
- All-sky cameras for sky quality monitoring and wow timelapses

**Science / Discovery Mode** (Phase 3 — Sprint 10+):
Analysis and discovery mechanics. Raw data → knowledge.
- Stratified knowledge: each object has 6+ levels of discoverable properties
- Manual analysis tools for critical discoveries (periodograms, spectral fitting)
- Automatic analysis for routine measurements
- Confirmation required: 2+ independent detections to confirm a discovery
- Survey mode: scan sky areas for new candidates
- Pointed mode: targeted observation of known/suspected objects

**Procedural Universe** (Sprint 07, extended Sprint 11+):
What makes Parallax infinite.
- Seed-based deterministic generation
- Every object fully exists in the engine with all properties
- Observation reveals properties, not generates them
- Sub-universe hierarchy: M31 → stars → planets → surfaces
- Transient events (supernovae, novae, GRBs) procedural in time and space

### 5.5 The Two Data Layers

Parallax separates two conceptually distinct data layers:

**Universe** (ground truth — Sprint 07):
Contains every object that exists, with all its properties, deterministic from seed.
Does not care what the player knows. Does not change based on gameplay.

**Knowledge Database** (player state — Sprint 08):
What the player has discovered. Initialized at game start with the "historical
knowledge" — i.e., L1-L3 properties of all real catalog objects, because the
scientific community has already catalogued them. The player adds:
- L4+ properties of historical objects (deep characterization)
- Existence and all properties of procedural objects
- Detection confirmation status (candidate vs confirmed)
- Observation history per object

**Rule for rendering**: the skychart displays `KnowledgeDatabase`, not `Universe`.
The Universe is only consulted through the Observation subsystem (which takes
time, requires instruments, produces data records).

### 5.6 Sub-Universe Hierarchy

Every CelestialObject can be a "container" with its own sub-universe.
Entering a container (resolving its interior) requires angular resolution
better than the container's `containment_angular_radius_arcsec`.

```
Milky Way (implicit top-level)
  ├── Hipparcos stars (catalog — L1-L3 historical)
  │   └── Sirius (is_container=true, radius=7.5")
  │       ├── Sirius B (catalog — historical, L2+ needs resolving)
  │       └── Planets (procedural — need L5 to resolve, then L6 for surface)
  ├── Tycho-2 stars (catalog)
  │   └── HIP XYZ
  │       └── Planetary system (procedural, unless specifically catalogued)
  ├── M31 (DSO — L1-L3 historical)
  │   ├── Individual stars (procedural, billions)
  │   ├── Nebulae (procedural)
  │   ├── Globular clusters (procedural, modeled on real distribution)
  │   └── Historical SN (e.g., S Andromedae 1885 — catalog)
  └── Procedural stars/galaxies (beyond catalogs, discoverable)
```

### 5.7 Data Flow — Skychart Mode (Single Frame)

```
1. Timer tick → delta time
2. Input poll (SDL2 events)
3. Update simulation time (JD += dt × time_scale / 86400)
4. Compute LST
5. Update observation sessions (Sprint 08+): tick active sessions
6. Harvest completed sessions → analysis → Knowledge DB updates
7. Universe::query_fov for render bounds
8. Filter through Knowledge: historical always, procedural only if known
9. For each rendered object:
   a. RA/Dec → Alt/Az → screen coords
   b. Style by discovery status (historical / confirmed / candidate)
10. Render: sky background → starfield → overlays → UI panels
11. Present
```

### 5.8 Universe Engine Query Pattern (Sprint 07+)

All astronomical data flows through a single `Universe` object.
Skychart, imaging, and science modes all query the same engine.

```
Universe::query_fov(ra, dec, radius, mag_limit, flags)
  → StarCatalogProvider     (Tycho-2, future Gaia)
  → DsoCatalogProvider      (Messier, future NGC/IC)
  → SolarSystemProvider     (Sun, Moon, planets)
  → ProceduralProvider      (stars, galaxies, nebulae beyond catalogs)
  → merged, sorted result: vector<CelestialObject>
```

Every object has a 64-bit ID: `(type << 56) | source_id`.
Every object is a `CelestialObject` with type-specific data in a union.
Providers implement a common `DataProvider` interface.
New data sources are added as new providers — the query interface doesn't change.

---

## 6. Universe Architecture

### 6.1 Hybrid Model

| Layer               | Source                     | Object Count     | Phase |
|---------------------|----------------------------|------------------|-------|
| Bright stars        | Hipparcos / Tycho-2        | ~2.5 million     | 1     |
| Full stellar        | Gaia DR3                   | ~1.8 billion     | 1     |
| Deep sky objects    | NGC / IC / Messier         | ~13,000          | 1     |
| Solar system        | JPL Horizons / VSOP87      | Major bodies     | 1     |
| Procedural stars    | Seed-based IMF synthesis   | Trillions        | 2     |
| Procedural galaxies | Hubble classification gen  | Billions         | 2     |
| Transient events    | Seed-based SN/nova/GRB     | Dynamic          | 3     |
| Exoplanets          | NASA Archive + procedural  | Millions         | 3     |

### 6.2 Catalog Binary Format

```
Header (64 bytes):
  magic:       "PLX_CAT\0"     (8 bytes)
  version:     uint32
  entry_count: uint64
  entry_size:  uint32
  index_type:  uint32          (0=HEALPix, 1=Octree)
  index_offset: uint64
  data_offset:  uint64
  reserved:    padding to 64

Spatial Index:
  HEALPix pixel → file offset range
  Enables FOV-based streaming

Star Entry (compact, 32 bytes):
  ra:          float64   (radians)
  dec:         float64   (radians)
  mag_v:       float16   (visual magnitude)
  mag_b:       float16   (blue magnitude)
  parallax:    float16   (mas)
  spectral:    uint8     (encoded spectral type)
  flags:       uint8     (variable, binary, etc.)
  source_id:   uint32    (cross-reference)
```

---

## 7. Development Phases

### Phase 1: Planetarium Core ✅ COMPLETE (Sprints 01-05)
- Vulkan initialization + rendering pipeline
- SDL2 window + input handling
- Star catalogs: Hipparcos + Tycho-2 (2.5M stars) with spatial indexing
- Coordinate transforms (J2000 ↔ horizontal ↔ screen)
- Magnitude-based star rendering (skychart mode)
- Sky background gradient
- Time simulation (sidereal clock, acceleration)
- Constellations (Stellarium verified data), coordinate grids
- Messier DSO catalog (110 objects)
- Horizon + cardinal markers
- Stellarium-style interactive UI (toolbar, panels, selection, info)

### Phase 2: Universe & Discovery Foundations (Sprints 06-08)
- Solar system with Meeus ephemeris (Sun, Moon, planets) — Sprint 06
- Skychart atmosphere toggle (on/off for planning) — Sprint 06
- Universe Engine: unified query across all data sources — Sprint 07
- Procedural foundation (stars beyond Tycho-2 limit) — Sprint 07
- Knowledge System: stratified properties, historical vs discovered — Sprint 08
- Observation Sessions: time-based, SNR accumulation — Sprint 08
- Sub-universe hierarchy architecture — Sprint 08
- Mock instrument for end-to-end vertical slice — Sprint 08

### Phase 3: UI Shell & Real Instruments (Sprints 09-12)
- UI Shell refactor: tab-based architecture, persistent sidebar/topbar/statusbar — Sprint 09
- Lunar base setting (Tycho Crater) — Sprint 09
- Multi-pane split-screen with drag-resize — Sprint 09
- Telescope model (aperture, focal length, FOV, angular resolution) — Sprint 10
- Sensor simulation (CCD/CMOS, noise, dark current, gain, exposure) — Sprint 10
- Imaging mode rendering (atmosphere applied, PSF, bloom, diffraction spikes) — Sprint 10
- All-sky camera (sky quality monitoring, timelapses) — Sprint 10
- Astrophotography pipeline (stacking, calibration, stretching) — Sprint 11
- Photometry analysis tools (light curves, periodograms) — Sprint 11
- First real discovery loop: find transiting exoplanets, variable stars — Sprint 11

### Phase 4: Advanced Science (Sprints 13-16)
- Spectroscopy (low and high resolution)
- Radial velocity measurements
- Deeper catalogs (NGC/IC, Gaia DR3 streaming)
- Sub-universe activation (resolving M31 into stars, Sirius into planets)
- Procedural exoplanet systems
- Discovery gamification (milestones, publication system)

### Phase 5: Visual Identity — The "Wow" (two dedicated rendering sprints)
Placed AFTER instrument/science foundations are solid. Do NOT anticipate.

**"All-Sky Wow" — contemplative beauty (the sky captured by all-sky cameras):**
The skychart stays schematic forever — it is a navigation map, not a beauty shot.
The contemplative "wow" belongs to the ALL-SKY CAMERAS, which are real physical
instruments (fisheye / equiangular) that capture the whole sky beautifully.
- Milky Way as volumetric structure (luminous band, dark dust lanes)
- Diffuse nebulae visible to the wide field (volumetric/billboard)
- Bloom, diffraction spikes, glare on bright sources
- Earth seen from the Moon (iconic hanging crescent, in the all-sky frame)
- Twilight, airglow, zodiacal light, atmospheric scattering (Earth stations)
- Lunar landscape horizon in the all-sky frame (from Tycho base)
- Real all-sky images: savable, shareable
- Timelapse capture (sequence of frames over accelerated time)
- Screenshot mode, color grading

**"Imaging Wow" — earned beauty (what the Glasswing Array reveals):**
- Photorealistic resolved procedural objects
- Nebula filaments, HII regions, star-forming structures
- Resolved galaxy detail (interferometry/sub-universe permitting)
- Planetary surface maps rendered beautifully
- Multispectral false-color compositing with artistic control

### Phase 6: Frontier Instruments (later sprints)
- Earth-Moon interferometry expansion (EHT-scale, more stations)
- Multi-instrument coordination (network of telescopes)
- Radio, X-ray, polarimetry
- Transient event system (supernovae, novae, GRBs)
- Sci-fi instruments: direct imaging of exoplanets, surface maps
- Cosmological survey mode (distant quasars, high-z galaxies)
- Multi-observatory management (Moon + Earth + orbital)
- Research progression, career system

---

## 7b. Visual Identity (the "Wow" aesthetic)

**Direction: Space Engine photorealism filtered through high-resolution pixel-art sensibility.**

**Primary style reference for celestial objects (stars, nebulae, galaxies, all real
and procedural objects): the videogame "Substrate: Emergence" (Petri Dish Games).**

Substrate: Emergence renders fluorescent cells with psychedelic pixel art inspired
by fluorescence microscopy — luminous organic structures glowing against darkness,
saturated emission colors, hypnotic emergent patterns. This maps directly onto
astronomical imaging:

- **Fluorescence microscopy ≈ emission nebulae**: both are luminous structures
  emerging from a dark background. Nebulae ARE cosmic fluorescence (H-alpha red,
  OIII teal, SII gold — emission lines as "fluorophores")
- **Glowing structures with soft falloff**: stars and nebulae should have that
  fluorescent luminosity — light that feels emitted, not painted
- **Saturated but physically-motivated palettes**: colors encode real physics
  (emission lines, blackbody temperature, redshift) rendered with the vivid
  intensity of fluorescence imaging
- **High-res pixel art discipline**: ordered dithering, controlled palettes,
  coherent grain — the Substrate: Emergence look of fine pixel detail forming
  larger organic/cosmic structure
- **Dark field dominance**: the void is truly black; objects bloom out of it

Applied per object type:
- Stars: luminous points with fluorescent-like glow cores, diffraction accents
- Emission nebulae: layered translucent filaments in emission-line palettes,
  like fluorescent-stained structures
- Galaxies: dense luminous cell-colony-like cores with resolved star grain
- Procedural planets/surfaces: same palette discipline, multispectral falsecolor

The aesthetic target remains:
- **Photorealistic cosmic structure** like Space Engine — real morphology,
  believable physics, true sky brightness relationships
- **Substrate: Emergence rendering language** — fluorescent luminosity,
  psychedelic-but-scientific palettes, high-res pixel art discipline
- **Retro scientific instrument feel** — UI and overlays stay terminal-green
  and CRT-flavored; the sky itself is rich and deep

When the rendering sprints arrive:
- Dithering as an aesthetic choice, not a limitation
- Tone mapping preserving the sky's vast dynamic range
- Palettes informed by real astronomical imaging but stylized for coherence
- Bloom/diffraction that feel optical, not video-game-glowy
- A consistent recognizable "Parallax look" — neither pure sim nor pure stylization

The "wow" lives in two places:
- **Earned wow** (Glasswing Array imaging): beauty you discover with the precision
  instrument — grows through Sprints 10a/10b/11/13 naturally as resolution improves
- **Contemplative wow** (all-sky cameras): the sky's inherent beauty captured by
  wide-field all-sky instruments — delivered by the dedicated rendering sprint.
  The SKYCHART is NOT a beauty surface — it stays schematic (navigation map).

IMPORTANT: Do NOT implement advanced visual features until the dedicated rendering
sprints. Earlier sprints keep functional schematic rendering. Building beauty on
unfinished foundations means rebuilding it. Discipline now, wow later.

---

## 8. UI Architecture (Sprint 09+)

The game's UI follows a persistent shell pattern, NOT a planetarium-with-panels.

```
┌──────────────────────────────────────────────────────────────────┐
│ TOP BAR: app │ location │ time │ atmosphere │ FPS                │
├─────────┬────────────────────────────────────────────────────────┤
│         │                                                         │
│ SIDEBAR │  TAB AREA (recursive panes with draggable splitters)   │
│ - Instr.│                                                         │
│ - Tabs  │  Multiple tabs can be visible side-by-side             │
│ - Time  │  Each tab is alive even when not visible (state persists)│
│         │                                                         │
├─────────┴────────────────────────────────────────────────────────┤
│ STATUS BAR: active sessions │ notifications │ time scale          │
└──────────────────────────────────────────────────────────────────┘
```

**Tab set:**
- PLANETARIUM — skychart (one tab among many, not the primary view)
- IMAGING — live telescope feed (Sprint 10+)
- SPECTROSCOPY — spectrum visualization (Sprint 11+)
- ANALYSIS — data analysis workspace (Sprint 10+)
- ARCHIVE — collected data records
- ENCYCLOPEDIA — knowledge browser
- ALL-SKY — all-sky camera (Sprint 10+)
- BASE — lunar base management

**Key principles:**
- Sidebar is fixed (always visible)
- Tab panes use docking/tiling, not floating windows
- Multiple tabs can be visible simultaneously via split-screen
- Tab state persists when navigating away
- All tabs available from start (placeholders until activated)
- Retro green terminal aesthetic throughout

## 9. Game Setting

Default player location: **Tycho Crater Base, Moon**.
- Lat -43.31°, Lon -11.36°, Elev -1.2 km
- No atmosphere (vacuum)
- Earth visible as celestial object (Sprint 10+ to render)
- Foundation for Earth-Moon interferometry sci-fi gameplay (Sprint 17+)

Additional observer locations available (for instruments deployed there):
- Earth: La Palma, Mauna Kea, Paranal, McDonald Observatory
- (Future) Space orbit, Mars, other bodies

---

## 10. Current Sprint

**Sprint:** 06 — Solar System & Atmosphere Toggle (READY)
**Goal:** Sun, Moon, major planets via Meeus ephemeris; skychart atmosphere on/off toggle

See: `docs/sprints/sprint_06.md`
Prompts: `docs/sprints/sprint_06_prompts.md`

**Pending sprints (briefs written, awaiting implementation):**
- Sprint 06 — Solar System + Atmosphere Toggle
- Sprint 07 — Universe Engine (unified data architecture)
- Sprint 08 — Knowledge System + Observation Sessions ✅ COMPLETE
- Sprint 09 — UI Shell Refactor (tab-based, lunar base setting)
- Sprint 10a — Array Instrument + physical SNR + multispectral imaging (total-power)
- Sprint 10b — Interferometry / aperture synthesis (planned, not yet written)

**Instrument concept:** "Glasswing Array" — multispectral EHT-inspired interferometer.
Stations on Moon (Tycho) + Earth (La Palma, Mauna Kea, Paranal). Starts in total-power
mode (10a), gains aperture synthesis for microarcsecond resolution (10b).

**Previous:** Sprint 01-05 ✅ — Phase 1 Planetarium Core complete

**Roadmap:**
```
Sprint 06:  Solar System + Atmosphere Toggle
Sprint 07:  Universe Engine (unify data, procedural foundation)
Sprint 08:  Knowledge System + Observation Sessions (mock E2E) ✅
Sprint 09:  UI Shell Refactor (tab system, lunar base)
Sprint 10a: Array instrument + physical SNR + multispectral imaging (total-power)
Sprint 10b: Interferometry / aperture synthesis (uv coverage, reconstruction)
Sprint 11:  Photometry/analysis tools + first discoveries + all-sky camera (functional)
Sprint 12:  Spectroscopy
Sprint 13:  Sub-universe activation (stars inside M31, planets in Sirius)
Sprint 14:  "All-Sky Wow" — Milky Way, nebulae, Earth-from-Moon, bloom, timelapse
Sprint 15:  "Imaging Wow" — photorealistic resolved objects, surface maps
Sprint 16+: Frontier instruments, transients, cosmological survey, career system
```

The "wow" rendering sprints (14-15) come AFTER solid foundations.
Earlier sprints keep functional schematic rendering — discipline now, wow later.

**Instrument: "Glasswing Array"** — EHT-inspired multispectral interferometer.
Starts total-power (10a), gains aperture synthesis (10b). Progression via more
stations, more spectral bands (Visible/IR unlocked → Mid-IR/Radio-K/Submm),
better (u,v) coverage and angular resolution.

---

## 9. AI Assistant Instructions

When generating code for this project:

1. **Always** follow the naming conventions in Section 3
2. **Always** use Allman brace style
3. **Always** add `#pragma once` to headers
4. **Always** follow the include order in Section 3.3
5. **Always** use `spdlog` for logging, never `std::cout`
6. **Always** check `VkResult` after Vulkan calls
7. **Never** use raw `new`/`delete`
8. **Never** use `using namespace` in headers
9. **Never** add dependencies not listed in Section 2
10. **Prefer** returning `std::optional` or `Result<T,E>` over throwing
11. **Prefer** `std::string_view` and `std::span` for non-owning params
12. **Prefer** designated initializers for configuration structs
13. **Keep** functions under 50 lines when possible
14. **Document** public APIs with `///` Doxygen-style comments
15. **Test** pure logic modules (astro, catalog) independently
16. **Namespace** everything under `parallax::module_name`
17. **Never** create pull requests or feature branches — work directly on the current branch
18. **Never** generate constellation or catalog data from AI knowledge — use verified astronomical databases

When unsure about an architectural decision, refer to Section 5 (layer model).
When unsure about data formats, refer to Section 6.
When unsure about what to build next, refer to Section 8 (current sprint).
