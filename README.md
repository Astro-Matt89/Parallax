# Parallax

**Parallax** is a ground-based astronomical observatory simulator written in C++20.  
The player never leaves Earth — the universe is explored exclusively through instruments.

## Vision

Parallax combines:
- Real astronomical catalogs (Hipparcos/Tycho bright stars, with Gaia DR3 integration planned)
- Physically accurate procedural universe generation (seeded, deterministic)
- Realistic optics simulation (diffraction, PSF, SNR)
- Atmospheric modelling (seeing, extinction, refraction, Bortle scale)
- Scientific discovery mechanics (parallax measurement, transit detection, SNR-gated confirmation)

## Architecture

```
src/
├── core/math/          # Vec3, celestial coordinate conversions, airmass
├── universe/           # Star catalog, Star data model, ProceduralGenerator (PCG/Kroupa IMF)
├── observatory/        # Telescope optics, AtmosphericModel, ObservingSession
├── rendering/          # StarField projection, ConsoleRenderer (terminal UI)
└── discovery/          # DiscoveryEngine (detection threshold, confirmation workflow)
tests/                  # 198 unit tests covering all subsystems
```

## Building

Requirements: **CMake ≥ 3.20**, a **C++20** compiler (GCC 13+ or Clang 16+).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## Running

```bash
./build/parallax          # interactive simulation demo
./build/parallax_tests    # unit test suite (198 tests)
```

## Core Systems

### Coordinate System (`core/math/coordinates.hpp`)
- Equatorial ↔ Horizontal (azimuth/altitude) conversion
- Hour angle, Local Sidereal Time
- Airmass (Pickering 2002), angular separation
- Atmospheric refraction (Saemundsson 1986)

### Star Catalog (`universe/star_catalog.hpp`)
- Grid-indexed spatial catalog (1°/cell)
- `query(centre, radius, mag_limit)` — magnitude-filtered cone search
- `findByName` / `findById`
- Built-in bright-star table: 24 Hipparcos stars (Sirius, Vega, Betelgeuse, Polaris, …)
- Distance modulus / parallax computed from catalog distances

### Procedural Generator (`universe/procedural_generator.hpp`)
- PCG-XSH-RR 64→32 pseudorandom number generator
- Kroupa (2001) broken-power-law Initial Mass Function
- Deterministic per-tile seed: same seed + same tile → identical stars
- Galactic density model (higher density near Milky Way plane)
- Generates spectral class, absolute magnitude, variable flag, proper motions

### Telescope & Optics (`observatory/telescope.hpp`)
- F-ratio, pixel scale, field of view
- Rayleigh diffraction limit
- Collecting area with central obstruction
- Photon flux (Johnson V zero-point)
- SNR: signal, sky background, read noise, dark current
- Limiting magnitude (binary search to SNR=5)
- Factory presets: 8″ SCT, 1-metre research reflector

### Atmospheric Model (`observatory/atmosphere.hpp`)
- Fried parameter r₀ from seeing FWHM
- Extinction (Rayleigh + Mie, airmass-weighted)
- Effective seeing FWHM (Kolmogorov X^0.6 scaling)
- Sky background from Bortle scale
- Temperature/pressure-corrected refraction

### Observatory Session (`observatory/observer.hpp`)
- Julian Date, GMST, LST
- Equatorial → Horizontal for current LST
- Atmospheric apparent magnitude and SNR for any target

### Discovery Engine (`discovery/discovery_engine.hpp`)
- SNR-gated detection threshold (SNR ≥ 5) and discovery threshold (SNR ≥ 7)
- Confirmation workflow: 3 independent detections required
- Parallax measurability: precision = 0.1 × diffraction_limit / √n_epochs
- Transit depth formula: `(r_planet/r_star)²`
- Minimum detectable planet radius from photometric SNR

### Rendering (`rendering/`)
- Gnomonic (tangent-plane) sky projection
- PSF FWHM: quadrature sum of seeing and diffraction limit
- Magnitude → intensity: logarithmic photometric scale
- Diffraction spike flag for reflectors on bright stars
- ASCII terminal renderer with brightness-mapped glyphs

## Tone

Parallax is quiet, intelligent, meditative, and scientific.  
It should feel like a real observing night — the silence of a dome opening,  
the first light frame appearing on screen.
