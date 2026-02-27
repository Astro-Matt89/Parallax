# Rendering Pipeline

## Overview

Parallax uses a multi-pass forward rendering pipeline optimized for astronomical visualization.
The renderer must handle two extreme cases simultaneously:
- Millions of faint stars (sub-pixel, sub-threshold intensity)
- A few extremely bright sources (bloom, diffraction spikes)

---

## Render Passes (Phase 1)

```
Pass 1: Sky Background
  → Fullscreen quad
  → Computes gradient from horizon (dark zenith → brighter horizon)
  → Inputs: observer altitude, azimuth, Bortle scale, sun position
  → Output: HDR color buffer

Pass 2: Starfield
  → Instanced point rendering (or small quads for bright stars)
  → Each star: screen position, apparent magnitude, color index (B-V)
  → Magnitude → point size mapping (log scale)
  → Magnitude → intensity mapping (Pogson formula)
  → Color: B-V → RGB via Planck function lookup table
  → Additive blending over sky background
  → Output: accumulated into HDR buffer

Pass 3: Post-Process
  → Bloom extraction (threshold on bright sources)
  → Gaussian blur (separable, multi-pass for large kernels)
  → Bloom composite
  → Tone mapping (Reinhard or ACES)
  → Dithering (ordered 4x4 Bayer matrix for retro aesthetic)
  → Output: LDR swapchain image
```

---

## Star Rendering Strategy

### The Problem
Rendering 1.8 billion stars is not feasible per frame.
Even with GPU instancing, the bottleneck is CPU-side culling and data transfer.

### The Solution: Magnitude-Layer Streaming

```
Layer 0: mag < 2.0     →   ~50 stars      → Always loaded
Layer 1: mag < 4.0     →   ~500 stars     → Always loaded
Layer 2: mag < 6.5     →   ~9,000 stars   → Naked eye limit, always loaded
Layer 3: mag < 10.0    →   ~340,000 stars → Load based on FOV
Layer 4: mag < 14.0    →   ~10M stars     → Load based on FOV + telescope
Layer 5: mag < 18.0    →   ~100M stars    → Stream on demand
Layer 6: mag < 21.0+   →   ~1.8B stars    → Deep field only
```

Per frame, the renderer only processes stars brighter than the current limiting magnitude
(determined by telescope aperture, exposure, atmospheric conditions).

### GPU Buffer Layout

```cpp
struct StarVertex   // Per-instance data, 16 bytes
{
    float x;        // Screen X (normalized device coords)
    float y;        // Screen Y
    float intensity; // Linear brightness after extinction
    float color;    // Encoded B-V color index
};
```

Stars uploaded to a large `VkBuffer` (storage buffer or vertex buffer).
Updated per frame from CPU-side catalog query results.
GPU renders via instanced draw with `gl_PointSize` or quad expansion in geometry/vertex shader.

### Magnitude-to-Size Mapping

```
Visual magnitude → apparent brightness:
  brightness = 10^(-0.4 * (mag - mag_zero))

Brightness → point size (pixels):
  size = clamp(base_size * sqrt(brightness / threshold), min_size, max_size)

Where:
  mag_zero = calibration magnitude (typically 0.0 for Vega)
  base_size = reference size at threshold brightness
  min_size = 1.0 px (faintest visible)
  max_size = 8.0 px (brightest: Sirius, planets)
```

### Color Rendering

B-V color index → surface temperature → RGB:

```
Precomputed LUT (256 entries):
  B-V range: [-0.4, +2.0]
  Maps to: blue-white → white → yellow → orange → red

Applied in fragment shader as tint on white base color.
```

---

## Sky Background Model (Phase 1: Simplified)

Phase 1 uses a parametric model:

```
Inputs:
  - Sun altitude (determines twilight state)
  - Observer zenith angle per pixel
  - Bortle scale (light pollution level)

Output per pixel:
  - Night: dark blue-black gradient (brighter toward horizon)
  - Twilight: orange/pink gradient blending to night
  - Light pollution: additive warm glow from horizon
```

Phase 2 replaces this with physically-based Rayleigh/Mie scattering.

---

## Post-Process: Bloom

Bloom simulates the optical glow around bright sources.

```
1. Extract bright pixels (threshold in HDR space)
2. Downsample to 1/4 resolution
3. Separable Gaussian blur (horizontal + vertical)
4. Multiple passes for wide kernel
5. Upsample and composite additively
```

Implemented as compute shaders for efficiency.

---

## Post-Process: Retro Dithering

Optional ordered dithering gives the retro aesthetic:

```
4x4 Bayer matrix applied in final pass:
  threshold = bayer[x % 4][y % 4] / 16.0
  color = floor(color * levels + threshold) / levels

Where levels = 32 (simulates limited color depth)
```

Toggleable. Can be combined with a subtle scanline effect.

---

## HDR Pipeline

Internal rendering uses HDR (16-bit float per channel).
This is critical because:
- Star brightness spans 20+ magnitudes (factor of 10^8)
- Bloom requires accurate bright-source extraction
- Tone mapping needs full dynamic range

```
HDR render target (VK_FORMAT_R16G16B16A16_SFLOAT)
  → Sky background (HDR)
  → Stars composited (additive HDR)
  → Post-process reads HDR → writes LDR swapchain
```

---

## Coordinate Pipeline (CPU → GPU)

```
Catalog (RA/Dec J2000)
  → Precession/nutation to current epoch
  → Aberration correction
  → Transform to horizontal (Alt/Az)
  → Atmospheric refraction
  → Project to screen (stereographic or gnomonic)
  → Upload screen positions to GPU buffer
```

All transforms done on CPU in double precision.
Only the final screen coordinates (float32) are sent to GPU.
