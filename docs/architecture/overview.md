# Architecture Overview

## Module Dependency Graph

```
                    ┌──────────────┐
                    │  Application │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────▼────┐ ┌────▼─────┐ ┌───▼────┐
        │    UI    │ │Observatory│ │Rendering│
        └─────┬────┘ └────┬─────┘ └───┬────┘
              │            │           │
              │       ┌────▼─────┐    │
              │       │  Astro   │◄───┘
              │       └────┬─────┘    │
              │            │          │
              │       ┌────▼─────┐   │
              └──────►│ Catalog  │◄──┘
                      └────┬─────┘
                           │
                    ┌──────▼───────┐
                    │    Vulkan    │
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │     Core     │
                    └──────────────┘
```

## Module Responsibilities

### Core (`parallax::core`)
Foundation layer. Zero game-specific logic.
- `Application` — lifecycle, main loop
- `Window` — SDL2 window, input events, Vulkan surface
- `Logger` — spdlog wrapper, dual-logger system
- `Timer` — high-resolution delta time, frame rate tracking
- `Config` — runtime settings, INI/JSON parsing
- `types.hpp` — common aliases (`f32`, `f64`, `u32`, `u64`, `Vec3d`, etc.)

### Vulkan (`parallax::vulkan`)
Thin abstraction over Vulkan API. Not a general-purpose engine — tailored to Parallax needs.
- `Context` — instance, device, queues, validation
- `Swapchain` — presentation, image management, recreation
- `Pipeline` — graphics and compute pipeline creation
- `Buffer` — vertex, index, uniform, storage buffers (via VMA)
- `Image` — textures, render targets, depth buffers
- `Descriptor` — descriptor set layout, pool, allocation
- `Command` — command pool, buffer recording
- `Shader` — SPIR-V loading, reflection (optional)
- `Sync` — fences, semaphores, frame synchronization

Design note: use VulkanMemoryAllocator (VMA) for all memory allocation.

### Catalog (`parallax::catalog`)
Astronomical data management. Pure data, no rendering.
- `StarEntry` — compact star data struct (32 bytes)
- `DeepSkyEntry` — DSO data struct
- `CatalogLoader` — memory-mapped binary file reader
- `SpatialIndex` — HEALPix sky partitioning for FOV queries
- `MagnitudeFilter` — magnitude-based LOD for streaming

Data pipeline:
```
Raw catalog (CSV/VOTable) → [offline tool] → Binary .plxcat → [runtime loader] → SpatialIndex
```

### Astro (`parallax::astro`)
Pure astronomical computation. No side effects, fully testable.
- `Coordinates` — RA/Dec, Alt/Az, Galactic, ecliptic transforms
- `TimeSystem` — Julian Date, Modified JD, sidereal time, UTC/TT/TAI
- `Precession` — IAU 2006 precession, nutation, frame bias
- `Aberration` — annual + diurnal aberration
- `Refraction` — atmospheric refraction (Bennett formula or Meeus)
- `Atmosphere` — extinction, sky brightness, seeing model

All functions take explicit parameters (no hidden state).
Double precision (`f64`) for all astronomical coordinates.

### Rendering (`parallax::rendering`)
High-level rendering orchestration.
- `Renderer` — frame graph, render pass sequencing
- `Starfield` — instanced point rendering, magnitude → size/brightness mapping
- `SkyBackground` — gradient from horizon, light pollution model
- `PostProcess` — bloom for bright sources, tone mapping, dithering

Rendering pipeline per frame:
```
1. Sky background pass (fullscreen quad)
2. Starfield pass (instanced points / quads)
3. DSO pass (textured billboards) [Phase 2]
4. Planet pass (sphere rendering) [Phase 2]
5. Post-process pass (bloom + tone map + dither)
6. UI overlay pass
```

### Observatory (`parallax::observatory`)
Game simulation layer.
- `Telescope` — aperture, focal length, FOV, resolution limit
- `Sensor` — CCD/CMOS model, noise, exposure, gain
- `Site` — geographic location, elevation, local conditions
- `Weather` — procedural cloud cover, seeing, transparency
- `Session` — current observing state, time flow, pointing

### UI (`parallax::ui`)
Retro console interface.
- `HUD` — on-screen information panels
- `Panel` — modular panel system (coordinates, time, telescope info)
- Custom GPU-rendered pixel font [Phase 1 can use bitmap font]

---

## Key Technical Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Coordinate precision | `double` (f64) | Arcsecond-level accuracy requires it |
| Star storage | 32 bytes packed struct | 1.8B Gaia entries × 32B = ~57 GB (streamed) |
| Spatial indexing | HEALPix (nested) | Standard for sky surveys, O(1) pixel lookup |
| Memory allocation | VMA | Vulkan memory management is error-prone |
| Shader compilation | Offline GLSL → SPIR-V | No runtime shader compilation |
| Frame sync | 2 frames in flight | Balance latency vs throughput |
| Time representation | Julian Date (f64) | Universal astronomical standard |

---

## Threading Model (Phase 1: Minimal)

```
Main Thread:     Input → Update → Record commands → Submit → Present
```

Future (Phase 2+):
```
Main Thread:     Input → Update → Record commands → Submit → Present
Catalog Thread:  Async FOV queries, magnitude filtering, streaming
Compute Thread:  Procedural generation, PSF computation
```

Phase 1 is single-threaded. Threading introduced in Phase 2 for catalog streaming.
