# Atmosphere Model

## Overview

The atmosphere is the primary obstacle between the observer and the universe.
Every photon traverses it. Parallax must simulate this accurately because
it directly affects what the player can observe and how.

---

## Phase 1: Essential Atmospheric Effects

### 1. Atmospheric Refraction

Light bends as it passes through the atmosphere, shifting apparent positions upward.

**Model:** Bennett formula (accurate to ~0.07' for altitudes > 5°)

```
R = 1 / tan(h + 7.31 / (h + 4.4))   [arcminutes]

Where:
  h = true altitude in degrees
  R = refraction correction (add to true altitude for apparent altitude)

Corrections for non-standard conditions:
  R_corrected = R × (P / 1010) × (283 / (273 + T))

Where:
  P = pressure in mbar
  T = temperature in °C
```

Applied during coordinate transform (RA/Dec → Alt/Az → apparent Alt/Az).

**Edge cases:**
- Below 5° altitude: refraction model becomes unreliable; use Meeus extended table
- At horizon (0°): refraction ≈ 34' (half a degree — the entire solar disk)
- Below horizon: objects can be visible due to refraction (sun visible after geometric sunset)

### 2. Atmospheric Extinction

The atmosphere absorbs and scatters light, dimming celestial objects.
Effect increases with airmass (longer path through atmosphere at low altitudes).

**Airmass calculation:**

```
X(z) = 1 / (cos(z) + 0.025 × exp(-11 × cos(z)))   [Rozenberg 1966]

Where:
  z = zenith angle
  X = airmass (1.0 at zenith, ~2.0 at 60°, ~5.5 at 80°, ~40 at horizon)
```

**Extinction:**

```
m_observed = m_true + k × X

Where:
  k = extinction coefficient (mag/airmass)
    k_V ≈ 0.12 (excellent site, visual band)
    k_V ≈ 0.20 (average site)
    k_V ≈ 0.40 (poor conditions / light pollution)
    k_B ≈ k_V × 1.3 (blue band, higher extinction)

Color-dependent extinction (Rayleigh scattering ∝ λ^-4):
  k(λ) = k_0 × (550nm / λ)^4   [simplified]
```

This means: blue stars dim more than red stars near the horizon.
The rendering pipeline must apply per-star extinction based on altitude and color.

### 3. Sky Background Brightness

The sky is never truly dark. Background brightness determines the faintest detectable magnitude.

**Sources of sky brightness:**
- Airglow (natural atmospheric emission): ~21.5-22.0 mag/arcsec² at zenith
- Zodiacal light (sunlight scattered by interplanetary dust)
- Light pollution (dominant factor for most sites)
- Moonlight (when Moon is above horizon)

**Bortle Scale model (Phase 1: parametric):**

| Bortle | Description          | Zenith SB (mag/arcsec²) | Naked-eye limit |
|--------|----------------------|-------------------------|-----------------|
| 1      | Excellent dark site  | 21.99-22.00             | 7.6-8.0         |
| 2      | Typical dark site    | 21.89-21.99             | 7.1-7.5         |
| 3      | Rural sky            | 21.69-21.89             | 6.6-7.0         |
| 4      | Rural/suburban        | 20.49-21.69            | 6.1-6.5         |
| 5      | Suburban             | 19.50-20.49             | 5.6-6.0         |
| 6      | Bright suburban      | 18.94-19.50             | 5.1-5.5         |
| 7      | Suburban/urban       | 18.38-18.94             | 4.6-5.0         |
| 8      | City                 | 17.80-18.38             | 4.1-4.5         |
| 9      | Inner city           | < 17.80                 | < 4.0           |

**Sky brightness gradient:**

```
SB(z) = SB_zenith - 2.5 × log10(1 + k_lp × X(z))

Where:
  SB_zenith = sky brightness at zenith (from Bortle scale)
  k_lp = light pollution gradient coefficient
  X(z) = airmass at zenith angle z

Near horizon: sky is always brighter (more atmosphere, scattered light).
```

### 4. Limiting Magnitude

The faintest star visible depends on:

```
m_lim = m_lim_0 - 5 × log10(1 + 10^(0.4 × (SB - SB_0)))

Simplified for telescope:
  m_lim ≈ 2.7 + 5 × log10(D_mm)   [visual, dark sky]

Where:
  D_mm = telescope aperture in mm

With atmosphere and sky background:
  m_lim_effective = m_lim - k × X - SB_penalty
```

This directly controls which catalog magnitude layers are queried.

---

## Phase 2: Advanced Effects (Not in Sprint 01)

### Seeing (Atmospheric Turbulence)

```
Fried parameter r₀: coherence length of atmosphere
  r₀ ≈ 5-20 cm (typical ground-based sites)
  
Seeing FWHM ≈ 0.98 × λ / r₀
  At λ = 550nm, r₀ = 10cm: seeing ≈ 1.1 arcsec

Effect: point sources become disks
Implementation: convolve star PSF with Gaussian of FWHM = seeing
```

### Scintillation (Twinkling)

```
Intensity variance:
  σ²_I ∝ D^(-4/3) × X^(7/4) × exp(-h/h_0)

Where:
  D = aperture diameter
  h = turbulent layer height

Small telescopes see more scintillation.
Large telescopes (> 20cm) average it out.
```

### Chromatic Dispersion

```
At low altitudes, atmosphere acts as a weak prism.
Blue light refracted more than red.
Effect: stars near horizon show color fringing.

Dispersion ≈ 1 arcsec between 400nm and 700nm at 45° altitude
            ≈ 5 arcsec at 20° altitude
```

---

## Implementation Architecture

```cpp
namespace parallax::astro
{
    struct AtmosphereParams
    {
        float pressure_mbar = 1013.25f;
        float temperature_c = 15.0f;
        float humidity_pct = 50.0f;
        float extinction_coeff = 0.20f;     // mag/airmass
        float bortle_scale = 4.0f;
        float seeing_arcsec = 2.0f;         // Phase 2
    };

    class Atmosphere
    {
    public:
        explicit Atmosphere(const AtmosphereParams& params);

        /// Atmospheric refraction correction (returns delta altitude in radians)
        [[nodiscard]] double refraction(double true_altitude_rad) const;

        /// Airmass at given zenith angle
        [[nodiscard]] double airmass(double zenith_angle_rad) const;

        /// Extinction in magnitudes for given altitude and wavelength
        [[nodiscard]] float extinction(double altitude_rad, float wavelength_nm = 550.0f) const;

        /// Sky background brightness at given zenith angle (mag/arcsec²)
        [[nodiscard]] float sky_brightness(double zenith_angle_rad) const;

        /// Effective limiting magnitude at given altitude for given aperture
        [[nodiscard]] float limiting_magnitude(double altitude_rad, float aperture_mm) const;

    private:
        AtmosphereParams m_params;
    };
}
```

All methods are `const`, pure functions on input parameters. Fully testable.
