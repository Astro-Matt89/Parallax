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

### 5.3 Data Flow (Single Frame)

```
1. Timer tick → delta time
2. Input poll (SDL2 events)
3. Observatory update (time, weather, pointing)
4. Catalog query (visible stars for current FOV + magnitude limit)
5. Astro transforms (RA/Dec → Alt/Az → screen coords)
6. Atmosphere apply (refraction, extinction, seeing)
7. Render starfield (instanced draw, GPU)
8. Render sky background (gradient, light pollution)
9. Post-process (bloom, tone map, PSF convolution)
10. UI overlay (HUD panels)
11. Present swapchain image
```

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

### Phase 1: Planetarium Core (CURRENT)
- Vulkan initialization + rendering pipeline
- SDL2 window + input handling
- Star catalog loading (Hipparcos/Tycho-2 first, then Gaia)
- Coordinate transforms (J2000 ↔ horizontal)
- Magnitude-based star rendering
- Basic atmospheric model (refraction, extinction)
- Sky background gradient
- Time simulation (sidereal clock)
- Basic telescope model (FOV, magnification)

### Phase 2: Deep Universe
- Procedural galaxy generation
- Deep sky object rendering
- Full atmospheric simulation (seeing, scintillation)
- CCD/CMOS sensor simulation
- Astrophotography pipeline
- Solar system integration

### Phase 3: Discovery & Gameplay
- Discovery mechanics (detection, confirmation)
- Career progression
- Research funding system
- Multi-observatory management
- Transient event system

---

## 8. Current Sprint

**Sprint:** 01 — Foundation
**Goal:** Vulkan context + SDL2 window + render black sky + single triangle test

See: `docs/sprints/sprint_01.md`

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

When unsure about an architectural decision, refer to Section 5 (layer model).
When unsure about data formats, refer to Section 6.
When unsure about what to build next, refer to Section 8 (current sprint).
