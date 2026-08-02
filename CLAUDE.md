# PARALLAX — Project Context for AI Assistants

> Ground-Based Astronomical Observatory Simulator
> Language: C++20 | Renderer: Vulkan | Platform: Windows + Linux

\---

## 1\. Project Identity

**Parallax** is a ground-based astronomical observatory simulator.
The player never leaves Earth. The universe is explored exclusively through instruments.

Core pillars:

* Scientific realism over spectacle
* Instruments as primary gameplay interface
* Hybrid universe: real astronomical catalogs + procedural generation
* Retro scientific terminal aesthetic with cosmic depth
* Discovery-driven progression

Comparable to: SpaceEngine constrained to terrestrial observation.
Not a space shooter. Not arcade sci-fi. Observational science.

\---

## 2\. Technology Stack

|Component|Choice|Notes|
|-|-|-|
|Language|C++20|GCC 12+ / MSVC 17.4+ / Clang 15+|
|Build System|CMake 3.25+||
|Package Manager|vcpkg (manifest mode)||
|Rendering API|Vulkan 1.3|Full control, compute shaders|
|Window / Input|SDL2|Vulkan surface via SDL\_Vulkan|
|Math|GLM|Header-only, GLSL-compatible|
|Logging|spdlog|Async, fast, header-only|
|Shader Language|GLSL → SPIR-V|Compiled offline via glslangValidator|
|Catalog Format|Custom binary + spatial index|Memory-mapped, HEALPix/Octree|

### Explicitly NOT in scope (for now)

* ImGui (may add later for dev tools)
* stb\_image (evaluate when needed)
* Eigen (GLM sufficient)

\---

## 3\. C++ Coding Standard

### 3.1 Naming Conventions

|Element|Convention|Example|
|-|-|-|
|Namespaces|`snake\_case`|`parallax::rendering`|
|Classes / Structs|`PascalCase`|`StarRenderer`, `CatalogEntry`|
|Functions / Methods|`snake\_case`|`render\_frame()`, `load\_catalog()`|
|Local variables|`snake\_case`|`star\_count`, `field\_of\_view`|
|Member variables|`m\_` prefix|`m\_aperture`, `m\_focal\_length`|
|Static members|`s\_` prefix|`s\_instance\_count`|
|Constants|`k` + PascalCase|`kMaxMagnitude`, `kDefaultFov`|
|Enum types|`PascalCase`|`SensorType`|
|Enum values|`PascalCase`|`SensorType::Monochrome`|
|Macros (avoid)|`PLX\_UPPER\_SNAKE`|`PLX\_ASSERT`, `PLX\_VERSION`|
|Template params|`PascalCase`|`template<typename CoordSystem>`|
|File names|`snake\_case`|`star\_renderer.hpp`, `.cpp`|

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
#include "star\_renderer.hpp"

// 2. Project headers
#include "core/logger.hpp"
#include "catalog/star\_entry.hpp"

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

* `std::unique\_ptr` / `std::shared\_ptr` for ownership
* `std::span` for non-owning array views
* `std::string\_view` for non-owning string references
* `std::optional` for nullable returns
* `std::expected` (or custom Result<T,E>) for error handling
* `std::format` for string formatting
* Designated initializers for config structs
* Concepts for template constraints
* `\[\[nodiscard]]` on functions with important return values
* Structured bindings

**Avoid:**

* Raw `new` / `delete`
* C-style casts (use `static\_cast`, `reinterpret\_cast`)
* Macros for constants or functions
* `using namespace` in headers
* Global mutable state (use dependency injection)
* Exceptions for control flow (reserve for truly exceptional cases)

### 3.5 Error Handling Strategy

```
Vulkan calls    → check VkResult, log + abort on failure
Resource loading → return std::optional or Result<T, Error>
Assertions      → PLX\_ASSERT(condition) in debug builds
Logging         → spdlog with levels: trace/debug/info/warn/error/critical
```

### 3.6 Formatting

* Indentation: 4 spaces (no tabs)
* Brace style: Allman (opening brace on new line)
* Max line length: 120 characters
* `clang-format` config provided in `.clang-format`

\---

## 4\. Project Structure

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
│   │   ├── sky\_background.hpp/cpp
│   │   └── post\_process.hpp/cpp # Bloom, tone mapping
│   │
│   ├── catalog/                 # Astronomical data
│   │   ├── star\_entry.hpp       # Star data structure
│   │   ├── catalog\_loader.hpp/cpp
│   │   ├── spatial\_index.hpp/cpp # HEALPix / Octree
│   │   └── magnitude\_filter.hpp/cpp
│   │
│   ├── astro/                   # Astronomical calculations
│   │   ├── coordinates.hpp/cpp  # RA/Dec, Alt/Az, transforms
│   │   ├── time\_system.hpp/cpp  # JD, sidereal time
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
│   ├── sky\_background.frag
│   ├── bloom.comp
│   └── compile\_shaders.sh       # SPIR-V compilation script
│
├── data/                        # Runtime data
│   ├── catalogs/                # Binary catalog files
│   └── config/                  # Default configs
│
├── tools/                       # Offline utilities
│   ├── catalog\_converter/       # Raw → binary converter
│   └── shader\_compiler/         # Batch SPIR-V compilation
│
├── tests/                       # Unit tests
│   ├── test\_coordinates.cpp
│   ├── test\_spatial\_index.cpp
│   └── CMakeLists.txt
│
└── docs/                        # Project documentation
    ├── architecture/            # Technical design docs
    │   ├── overview.md
    │   ├── rendering\_pipeline.md
    │   ├── catalog\_system.md
    │   └── atmosphere\_model.md
    └── sprints/                 # Sprint briefs for AI
        └── sprint\_01.md
```

\---

## 5\. Architecture Overview

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

* **Core** depends on nothing (except STL + spdlog)
* **Vulkan** depends on Core
* **Catalog** depends on Core (no rendering dependency)
* **Astro** depends on Core + GLM (pure math, no rendering)
* **Rendering** depends on Vulkan + Catalog + Astro
* **Observatory** depends on Astro + Catalog
* **UI** depends on Rendering + Observatory
* **Application** depends on everything

No circular dependencies. Each module testable in isolation.

### 5.4 Parallax Vision \& Project Modes

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

* Historical catalog objects (Hipparcos, Tycho-2, Messier) always visible
with L1-L3 properties (position, mag, color, basic classification)
* Procedural objects visible ONLY after player discovers them
* Deep properties (L4+) of historical objects must be measured by the player
* Toggle atmosphere on/off for observation planning
* Think: your personal research database, not Stellarium

**Imaging Mode** (Phase 2 — Sprint 09+):
What the telescope/sensor actually captures. The core of Parallax.

* Full atmospheric model: refraction, extinction, seeing, scintillation
* Optical simulation: telescope PSF, diffraction, aberrations, field curvature
* Sensor simulation: CCD/CMOS, noise, dark current, gain, bit depth
* Astrophotography pipeline: exposure, stacking, calibration, stretching
* Real integration time, realistic SNR progression
* All-sky cameras for sky quality monitoring and wow timelapses

**Science / Discovery Mode** (Phase 3 — Sprint 10+):
Analysis and discovery mechanics. Raw data → knowledge.

* Stratified knowledge: each object has 6+ levels of discoverable properties
* Manual analysis tools for critical discoveries (periodograms, spectral fitting)
* Automatic analysis for routine measurements
* Confirmation required: 2+ independent detections to confirm a discovery
* Survey mode: scan sky areas for new candidates
* Pointed mode: targeted observation of known/suspected objects

**Procedural Universe** (Sprint 07, extended Sprint 11+):
What makes Parallax infinite.

* Seed-based deterministic generation
* Every object fully exists in the engine with all properties
* Observation reveals properties, not generates them
* Sub-universe hierarchy: M31 → stars → planets → surfaces
* Transient events (supernovae, novae, GRBs) procedural in time and space

### 5.5 The Two Data Layers

Parallax separates two conceptually distinct data layers:

**Universe** (ground truth — Sprint 07):
Contains every object that exists, with all its properties, deterministic from seed.
Does not care what the player knows. Does not change based on gameplay.

**Knowledge Database** (player state — Sprint 08):
What the player has discovered. Initialized at game start with the "historical
knowledge" — i.e., L1-L3 properties of all real catalog objects, because the
scientific community has already catalogued them. The player adds:

* L4+ properties of historical objects (deep characterization)
* Existence and all properties of procedural objects
* Detection confirmation status (candidate vs confirmed)
* Observation history per object

**Rule for rendering**: the skychart displays `KnowledgeDatabase`, not `Universe`.
The Universe is only consulted through the Observation subsystem (which takes
time, requires instruments, produces data records).

### 5.6 Sub-Universe Hierarchy

Every CelestialObject can be a "container" with its own sub-universe.
Entering a container (resolving its interior) requires angular resolution
better than the container's `containment\_angular\_radius\_arcsec`.

```
Milky Way (implicit top-level)
  ├── Hipparcos stars (catalog — L1-L3 historical)
  │   └── Sirius (is\_container=true, radius=7.5")
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
3. Update simulation time (JD += dt × time\_scale / 86400)
4. Compute LST
5. Update observation sessions (Sprint 08+): tick active sessions
6. Harvest completed sessions → analysis → Knowledge DB updates
7. Universe::query\_fov for render bounds
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
Universe::query\_fov(ra, dec, radius, mag\_limit, flags)
  → StarCatalogProvider     (Tycho-2, future Gaia)
  → DsoCatalogProvider      (Messier, future NGC/IC)
  → SolarSystemProvider     (Sun, Moon, planets)
  → ProceduralProvider      (stars, galaxies, nebulae beyond catalogs)
  → merged, sorted result: vector<CelestialObject>
```

Every object has a 64-bit ID: `(type << 56) | source\_id`.
Every object is a `CelestialObject` with type-specific data in a union.
Providers implement a common `DataProvider` interface.
New data sources are added as new providers — the query interface doesn't change.

\---

## 6\. Universe Architecture

### 6.1 Hybrid Model

|Layer|Source|Object Count|Phase|
|-|-|-|-|
|Bright stars|Hipparcos / Tycho-2|\~2.5 million|1|
|Full stellar|Gaia DR3|\~1.8 billion|1|
|Deep sky objects|NGC / IC / Messier|\~13,000|1|
|Solar system|JPL Horizons / VSOP87|Major bodies|1|
|Procedural stars|Seed-based IMF synthesis|Trillions|2|
|Procedural galaxies|Hubble classification gen|Billions|2|
|Transient events|Seed-based SN/nova/GRB|Dynamic|3|
|Exoplanets|NASA Archive + procedural|Millions|3|

### 6.2 Catalog Binary Format

```
Header (64 bytes):
  magic:       "PLX\_CAT\\0"     (8 bytes)
  version:     uint32
  entry\_count: uint64
  entry\_size:  uint32
  index\_type:  uint32          (0=HEALPix, 1=Octree)
  index\_offset: uint64
  data\_offset:  uint64
  reserved:    padding to 64

Spatial Index:
  HEALPix pixel → file offset range
  Enables FOV-based streaming

Star Entry (compact, 32 bytes):
  ra:          float64   (radians)
  dec:         float64   (radians)
  mag\_v:       float16   (visual magnitude)
  mag\_b:       float16   (blue magnitude)
  parallax:    float16   (mas)
  spectral:    uint8     (encoded spectral type)
  flags:       uint8     (variable, binary, etc.)
  source\_id:   uint32    (cross-reference)
```

\---

## 7\. Development Phases

### Phase 1: Planetarium Core ✅ COMPLETE (Sprints 01-05)

* Vulkan initialization + rendering pipeline
* SDL2 window + input handling
* Star catalogs: Hipparcos + Tycho-2 (2.5M stars) with spatial indexing
* Coordinate transforms (J2000 ↔ horizontal ↔ screen)
* Magnitude-based star rendering (skychart mode)
* Sky background gradient
* Time simulation (sidereal clock, acceleration)
* Constellations (Stellarium verified data), coordinate grids
* Messier DSO catalog (110 objects)
* Horizon + cardinal markers
* Stellarium-style interactive UI (toolbar, panels, selection, info)

### Phase 2: Universe \& Discovery Foundations (Sprints 06-08)

* Solar system with Meeus ephemeris (Sun, Moon, planets) — Sprint 06
* Skychart atmosphere toggle (on/off for planning) — Sprint 06
* Universe Engine: unified query across all data sources — Sprint 07
* Procedural foundation (stars beyond Tycho-2 limit) — Sprint 07
* Knowledge System: stratified properties, historical vs discovered — Sprint 08
* Observation Sessions: time-based, SNR accumulation — Sprint 08
* Sub-universe hierarchy architecture — Sprint 08
* Mock instrument for end-to-end vertical slice — Sprint 08

### Phase 3: UI Shell \& Real Instruments (Sprints 09-12)

* UI Shell refactor: tab-based architecture, persistent sidebar/topbar/statusbar — Sprint 09
* Lunar base setting (Tycho Crater) — Sprint 09
* Multi-pane split-screen with drag-resize — Sprint 09
* Telescope model (aperture, focal length, FOV, angular resolution) — Sprint 10
* Sensor simulation (CCD/CMOS, noise, dark current, gain, exposure) — Sprint 10
* Imaging mode rendering (atmosphere applied, PSF, bloom, diffraction spikes) — Sprint 10
* All-sky camera (sky quality monitoring, timelapses) — Sprint 10
* Astrophotography pipeline (stacking, calibration, stretching) — Sprint 11
* Photometry analysis tools (light curves, periodograms) — Sprint 11
* First real discovery loop: find transiting exoplanets, variable stars — Sprint 11

### Phase 4: Advanced Science (Sprints 13-16)

* Spectroscopy (low and high resolution)
* Radial velocity measurements
* Deeper catalogs (NGC/IC, Gaia DR3 streaming)
* Sub-universe activation (resolving M31 into stars, Sirius into planets)
* Procedural exoplanet systems
* Discovery gamification (milestones, publication system)

### Phase 5: Visual Identity — The "Wow" (two dedicated rendering sprints)

Placed AFTER instrument/science foundations are solid. Do NOT anticipate.

**"All-Sky Wow" — contemplative beauty (the sky captured by all-sky cameras):**
The skychart stays schematic forever — it is a navigation map, not a beauty shot.
The contemplative "wow" belongs to the ALL-SKY CAMERAS, which are real physical
instruments (fisheye / equiangular) that capture the whole sky beautifully.

* Milky Way as volumetric structure (luminous band, dark dust lanes)
* Diffuse nebulae visible to the wide field (volumetric/billboard)
* Bloom, diffraction spikes, glare on bright sources
* Earth seen from the Moon (iconic hanging crescent, in the all-sky frame)
* Twilight, airglow, zodiacal light, atmospheric scattering (Earth stations)
* Lunar landscape horizon in the all-sky frame (from Tycho base)
* Real all-sky images: savable, shareable
* Timelapse capture (sequence of frames over accelerated time)
* Screenshot mode, color grading

**"Imaging Wow" — earned beauty (what the Glasswing Array reveals):**

* Photorealistic resolved procedural objects
* Nebula filaments, HII regions, star-forming structures
* Resolved galaxy detail (interferometry/sub-universe permitting)
* Planetary surface maps rendered beautifully
* Multispectral false-color compositing with artistic control

### Phase 6: Immersion \& Frontier (later sprints)

* **Audio / sonification**: every object has a sonic signature (like the sandbox) —
pulsar pulses, binary beat tones, stellar granulation, AGN jet-modulated noise,
planetary weather bands — plus instrument interference and background noise floor.
Sonification is a real astronomy technique; here it's immersion + data channel.
* **UI/UX Final**: the finished interface skin. The functional shell (Sprint 09)
gets its polished visual identity once all functional tabs exist (the proposed
mockups become the real look). Sits near the wow sprints.
* **Base upgrade gameplay**: expand/modify the array and telescopes in-game
(instruments are already data-driven per Section 9.1)
* Earth-Moon interferometry expansion (EHT-scale, more stations)
* Multi-instrument coordination (network of telescopes)
* Radio, X-ray, polarimetry
* Transient event system (supernovae, novae, GRBs)
* Sci-fi instruments: direct imaging of exoplanets, surface maps
* Cosmological survey mode (distant quasars, high-z galaxies)
* Multi-observatory management (Moon + Earth + orbital)
* Research progression, career system

\---

## 7b. Visual Identity (the "Wow" aesthetic)

**Direction: Space Engine photorealism filtered through high-resolution pixel-art sensibility.**

**Primary style reference for celestial objects (stars, nebulae, galaxies, all real
and procedural objects): the videogame "Substrate: Emergence" (Petri Dish Games).**

Substrate: Emergence renders fluorescent cells with psychedelic pixel art inspired
by fluorescence microscopy — luminous organic structures glowing against darkness,
saturated emission colors, hypnotic emergent patterns. This maps directly onto
astronomical imaging:

* **Fluorescence microscopy ≈ emission nebulae**: both are luminous structures
emerging from a dark background. Nebulae ARE cosmic fluorescence (H-alpha red,
OIII teal, SII gold — emission lines as "fluorophores")
* **Glowing structures with soft falloff**: stars and nebulae should have that
fluorescent luminosity — light that feels emitted, not painted
* **Saturated but physically-motivated palettes**: colors encode real physics
(emission lines, blackbody temperature, redshift) rendered with the vivid
intensity of fluorescence imaging
* **High-res pixel art discipline**: ordered dithering, controlled palettes,
coherent grain — the Substrate: Emergence look of fine pixel detail forming
larger organic/cosmic structure
* **Dark field dominance**: the void is truly black; objects bloom out of it

Applied per object type:

* Stars: luminous points with fluorescent-like glow cores, diffraction accents
* Emission nebulae: layered translucent filaments in emission-line palettes,
like fluorescent-stained structures
* Galaxies: dense luminous cell-colony-like cores with resolved star grain
* Procedural planets/surfaces: same palette discipline, multispectral falsecolor

The aesthetic target remains:

* **Photorealistic cosmic structure** like Space Engine — real morphology,
believable physics, true sky brightness relationships
* **Substrate: Emergence rendering language** — fluorescent luminosity,
psychedelic-but-scientific palettes, high-res pixel art discipline
* **Retro scientific instrument feel** — UI and overlays stay terminal-green
and CRT-flavored; the sky itself is rich and deep

When the rendering sprints arrive:

* Dithering as an aesthetic choice, not a limitation
* Tone mapping preserving the sky's vast dynamic range
* Palettes informed by real astronomical imaging but stylized for coherence
* Bloom/diffraction that feel optical, not video-game-glowy
* A consistent recognizable "Parallax look" — neither pure sim nor pure stylization

The "wow" lives in two places:

* **Earned wow** (Glasswing Array imaging): beauty you discover with the precision
instrument — grows through Sprints 10a/10b/11/13 naturally as resolution improves
* **Contemplative wow** (all-sky cameras): the sky's inherent beauty captured by
wide-field all-sky instruments — delivered by the dedicated rendering sprint.
The SKYCHART is NOT a beauty surface — it stays schematic (navigation map).

IMPORTANT: Do NOT implement advanced visual features until the dedicated rendering
sprints. Earlier sprints keep functional schematic rendering. Building beauty on
unfinished foundations means rebuilding it. Discipline now, wow later.

\---

## 7c. Procedural Engine Architecture (future sprints)

**Core principle: ONE parametric generator per object class. The difference between
a real catalog object and a procedural one is only WHERE THE PARAMETERS COME FROM.**

This guarantees visual coherence: M31 and a procedural galaxy beside it speak the
same rendering language. It scales to thousands of real objects. And it means
maintaining one generator, not two.

### The parametric pattern

```cpp
// Same generator, different parameter sources
GalaxyGenerator::generate(const GalaxyParameters\& params, u64 detail\_seed);

// Real object (M31): parameters from catalog data
params = {
    hubble\_type:      Sb,        // from RC3
    inclination\_deg:  77.0,      // from RC3
    position\_angle:   35.0,      // from RC3
    diameter\_arcmin:  178.0,     // from catalog
    arm\_count:        2,
    pitch\_angle\_deg:  8.0,       // from spiral-arm catalogs
    bulge\_fraction:   0.30,
    dust\_strength:    0.70,
    color\_index:      measured,  // from photometry
};
detail\_seed = hash("M31");       // only fine grain: individual star positions

// Procedural object (unknown galaxy): parameters derived from seed
params = GalaxyParameters::derive\_from\_seed(seed);  // all parameters
detail\_seed = seed;
```

The macroscopic morphology of real objects is **locked to observations**.
The seed controls only fine detail (star grain, filament noise, sub-structure).

### Three tiers of objects

|Tier|Objects|Parameter source|Effort|
|-|-|-|-|
|**Iconic**|\~30-50 famous (M31, M42, M45, M51, M104...)|Curated by hand: catalog data + artist-tuned extras (specific dust lanes, Trapezium structure, etc.)|Manual|
|**Known**|Rest of NGC/IC, all catalog DSOs|Preprocessed morphological catalogs (RC3, HyperLEDA)|Automated|
|**Procedural**|Everything beyond catalogs|Derived from seed|Automated|

All three tiers go through the **same renderer**. Only parameter provenance differs.

### Morphological catalog preprocessing

Following the Tycho-2 pattern: preprocess real morphological catalogs into
project binary/CSV format with the parameters the generator needs.

Sources:

* **RC3** (Third Reference Catalogue of Bright Galaxies) — Hubble type,
dimensions, inclination, position angle, surface brightness
* **HyperLEDA** — extended morphological parameters, velocity, distances
* **Spiral-arm pitch angle catalogs** — for spiral structure fidelity
* **Nebula catalogs** — emission line ratios (H-alpha/OIII/SII), sizes, types

Tool: `tools/prepare\_morphology.py` → `data/catalogs/galaxy\_morphology.csv`

For iconic objects, a curated overlay file adds artist-tuned parameters beyond
what standard catalogs provide: `data/catalogs/iconic\_objects.json`

### Interaction with the Knowledge System

Real objects have their morphological parameters as **historical knowledge (L1-L3)**.
The player sees M31 with its correct shape, inclination, and dust lanes immediately —
because humanity has already observed it.

Procedural objects have parameters that **exist in the engine but are hidden**.
Observation reveals them progressively:

```
M31 (real):
  Morphology known from start → rendered with correct shape when visible

Procedural galaxy:
  L1: point source (just detected)
  L3: rough morphology + type (characterized by imaging)
  L5: resolved structure, individual stars (sub-universe, interferometry)
```

The generator always produces the full object. The renderer shows only what the
player's knowledge level permits. This is the same pattern as everywhere else in
Parallax: **the universe is complete; observation reveals it.**

### Scientific accuracy requirements

The generator must produce physically plausible objects:

* **Galaxies**: Hubble sequence morphology, realistic mass-luminosity relations,
spiral arm pitch angles consistent with type, correct bulge-to-disk ratios,
stellar population colors by radius (bluer arms, redder bulge)
* **Nebulae**: emission line ratios consistent with excitation mechanism,
realistic H-alpha/OIII/SII color mapping, plausible filament structure
* **Planetary systems**: IMF-consistent, Roche limits respected, stable orbits,
habitability zones from stellar type
* **Large scale structure**: filaments, voids, clusters following observed
correlation functions

Do NOT invent physics. Use real astronomical relations. When the generator needs
a distribution (mass function, color-magnitude relation), use the published one.

\---

## 8\. UI Architecture (Sprint 09+)

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

* PLANETARIUM — skychart (one tab among many, not the primary view)
* IMAGING — live telescope feed (Sprint 10+)
* SPECTROSCOPY — spectrum visualization (Sprint 11+)
* ANALYSIS — data analysis workspace (Sprint 10+)
* ARCHIVE — collected data records
* ENCYCLOPEDIA — knowledge browser
* ALL-SKY — all-sky camera (Sprint 10+)
* BASE — lunar base management

**Key principles:**

* Sidebar is fixed (always visible)
* Tab panes use docking/tiling, not floating windows
* Multiple tabs can be visible simultaneously via split-screen
* Tab state persists when navigating away
* All tabs available from start (placeholders until activated)
* Retro green terminal aesthetic throughout

## 9\. Game Setting

Default player location: **Tycho Crater Base, Moon**.

* Lat -43.31°, Lon -11.36°, Elev -1.2 km
* No atmosphere (vacuum)
* Earth visible as celestial object (Sprint 10+ to render)
* Foundation for Earth-Moon interferometry sci-fi gameplay (Sprint 17+)

Additional observer locations available (for instruments deployed there):

* Earth: La Palma, Mauna Kea, Paranal, McDonald Observatory
* (Future) Space orbit, Mars, other bodies

### 9.1 Base Instrument Configuration (data-driven — mandatory from the start)

The player's base (Tycho + Earth stations) hosts instruments that MUST be
**data-driven**, never hardcoded. This is an architectural requirement from
day one — retrofitting configurability later is far more costly.

**Interferometric array (Tycho Base):**

* **Y-shaped array** (VLA-style) — three arms, good (u,v) coverage with few antennas
* **Selectable site extent: 1 km / 10 km / 100 km** — changes baselines, hence
angular resolution. Corresponds to the sandbox `scale` parameter.
* Antenna count sufficient to populate the three Y-arms at every scale
* Station geometry, count, and scale all come from configuration, NOT hardcoded

**Optical telescopes (the Trio, later sprint):** GW-UWF / GW-NF / GW-OWL, also
defined by a configuration table (already the pattern in the Trio spec).

**Requirement for all instruments:**

* Configuration from data (JSON): station count, geometry, site scale, apertures, bands
* No code assumption of a fixed number of stations or a fixed layout
* A config format that allows defining/modifying setups in dev now, and
(future) in-game upgrades later
* The upgrade/expansion GAMEPLAY (how the player grows the base) is planned for a
later sprint, but the instruments must be BUILT ready for it from the start

### 9.2 Free Pointing Across the Celestial Sphere

The player can point instruments at ANY point on the celestial sphere, constrained
only by real physics:

* Ephemerides (is the target up at this time from this station?)
* Occultations (Moon blocking Earth stations, Earth blocking Moon stations)
* Latitude / minimum elevation limits (EL\_MIN)

Pointing resolves through the Universe Engine (Sprint 07): the pointed direction
returns whatever is there — a real catalogued object, or a procedural object
generated deterministically from the position seed — with its stratified properties
(Knowledge System). The interferometric target model (Sprint 10b, 8 families) and
the procedural generator (Section 7c) are the SAME system: the objects you point at.

This is the Space Engine-like hybrid universe: real + procedural, from galaxies
down to individual planets, all pointable and observable.

\---

## 10\. Current Sprint

**Sprint:** 06 — Solar System \& Atmosphere Toggle (READY)
**Goal:** Sun, Moon, major planets via Meeus ephemeris; skychart atmosphere on/off toggle

See: `docs/sprints/sprint\_06.md`
Prompts: `docs/sprints/sprint\_06\_prompts.md`

**Sprint status:**

* Sprint 06 — Solar System + Atmosphere Toggle (brief written)
* Sprint 07 — Universe Engine (brief written)
* Sprint 08 — Knowledge System + Observation Sessions ✅ COMPLETE
* Sprint 09 — UI Shell Refactor (brief written)
* Sprint 10a — Array Instrument + physical SNR + multispectral imaging ✅ COMPLETE
(FITS export deferred — cfitsio removed due to vcpkg/Windows build issues;
PNG export via stb\_image\_write works; minimal FITS writer deferred)
* Sprint 10b — Interferometry / aperture synthesis (brief written, sandbox-based)
* Optical Imaging Trio — separate sprint AFTER 10b (spec: Parallax\_Optical\_Imaging\_Trio\_spec.md)

**Instrument concept:** "Glasswing Array" — multispectral EHT-inspired interferometer.
Stations on Moon (Tycho) + Earth (La Palma, Mauna Kea, Paranal). Total-power
mode (10a) done; aperture synthesis for microarcsecond resolution (10b) next.

**Glasswing Sandbox oracle:** `glasswing-sandbox-v1\_7.html` is the CONCEPTUAL oracle
for the interferometry pipeline. The C++ replicates its math (fixtures validate numbers
within tolerance: uv 1e-9, visibility 1e-7, image 1e-6) but may diverge in implementation.
RNG is mulberry32 (must match bit-for-bit). Export the 15-fixture battery from the
sandbox for cross-validation. Target model = 8 procedural families (BINARY, STAR,
PROTO\_DISK, NOVA, AGN, COMPACT, PLANETARY, PLANET\_RES) = the concrete procedural
generator of Section 7c.

**Previous:** Sprint 01-05 ✅ — Phase 1 Planetarium Core complete

**Roadmap:**

```
Sprint 06:  Solar System + Atmosphere Toggle
Sprint 07:  Universe Engine (unify data, procedural foundation)
Sprint 08:  Knowledge System + Observation Sessions (mock E2E) ✅
Sprint 09:  UI Shell Refactor (tab system, lunar base)
Sprint 10a: Array instrument + physical SNR + multispectral imaging (total-power) ✅
Sprint 10b: Interferometry / aperture synthesis (uv coverage, CLEAN, fixtures)
Sprint 10c: Optical Imaging Trio (GW-UWF/NF/OWL single-dish, PSF via MTF) — or fold into 11
Sprint 11:  Photometry/analysis tools + first discoveries + all-sky camera (functional)
Sprint 12:  Spectroscopy
Sprint 13:  Sub-universe activation (stars inside M31, planets in Sirius)
Sprint 14:  "All-Sky Wow" — Milky Way, nebulae, Earth-from-Moon, bloom, timelapse
Sprint 15:  "Imaging Wow" — photorealistic resolved objects, surface maps
Sprint 16:  Audio / sonification — per-object sonic signatures, interference, noise floor
Sprint 17:  "UI/UX Final" — the finished interface skin (mockups become reality)
Sprint 18+: Frontier instruments, transients, cosmological survey, base upgrades, career
```

**Note on sprint numbering:** audio (16) and final UI (17) sit with the immersion/
wow cluster. The instrument-upgrade GAMEPLAY (how the player expands the base) is a
later sprint too — but instruments are built data-driven from the start (Section 9.1)
so the upgrade mechanic can be layered on without rearchitecting.

The "wow" rendering sprints (14-15) come AFTER solid foundations.
Earlier sprints keep functional schematic rendering — discipline now, wow later.

**Instrument: "Glasswing Array"** — EHT-inspired multispectral interferometer.
Starts total-power (10a), gains aperture synthesis (10b). Progression via more
stations, more spectral bands (Visible/IR unlocked → Mid-IR/Radio-K/Submm),
better (u,v) coverage and angular resolution.

\---

## 11\. AI Assistant Instructions

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
11. **Prefer** `std::string\_view` and `std::span` for non-owning params
12. **Prefer** designated initializers for configuration structs
13. **Keep** functions under 50 lines when possible
14. **Document** public APIs with `///` Doxygen-style comments
15. **Test** pure logic modules (astro, catalog) independently
16. **Namespace** everything under `parallax::module\_name`
17. **Never** generate constellation or catalog data from AI knowledge — use verified astronomical databases
18. **Never** invent physical formulas, constants, or distributions — use published astronomical relations
19. **Procedural generators are parametric**: one generator per object class. Real objects
get parameters from catalogs; procedural objects derive them from seed. Same renderer.

When unsure about an architectural decision, refer to Section 5 (layer model).
When unsure about data formats, refer to Section 6.
When unsure about what to build next, refer to Section 8 (current sprint).

