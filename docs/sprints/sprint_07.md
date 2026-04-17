# Sprint 07 — Universe Engine

**Prerequisite:** Sprint 06 complete (Solar System, atmosphere toggle)
**Goal:** Unify all astronomical data into a single Universe Engine with a unified query interface, then add the procedural generation foundation.
**Deliverable:** All existing objects (stars, DSOs, solar system) migrated into one architecture. First procedural objects visible (stars beyond catalog limits). Skychart and future imaging mode both query the same engine.

---

## Overview

This is the most important architectural sprint in the project.
After Sprint 07, the question "what is at these coordinates?" has ONE answer
from ONE system, regardless of whether the object is a Tycho-2 star, M31,
Jupiter, or a procedurally generated star in another galaxy.

### What changes
- All separate data structures (`m_star_catalog`, `m_messier`, `m_solar_system`) are removed from Application
- A single `Universe` object owns all astronomical data
- One query interface: `query_fov()` returns a unified list of objects
- Skychart renderer consumes this unified list
- Procedural generation plugs into the same system

### What stays the same
- Vulkan pipeline, shaders, rendering code
- Coordinate transforms, time system
- UI, panels, toolbar, selection, HUD
- Sky background shader
- Constellation/grid/horizon overlays

---

## Architecture

### Universal Object

Every object in the universe — real or procedural — is represented by one type:

```cpp
namespace parallax::universe
{
    enum class ObjectType : u8
    {
        // Real catalog objects
        Star,
        SolarSystemBody,
        DeepSkyObject,
        Exoplanet,

        // Procedural objects
        ProceduralStar,
        ProceduralGalaxy,
        ProceduralNebula,
        ProceduralPlanet,

        // Transients
        Supernova,
        Nova,
        VariableStar
    };

    enum class DsoSubType : u8
    {
        Galaxy, Nebula, OpenCluster, GlobularCluster,
        PlanetaryNebula, SupernovaRemnant, Other
    };

    enum class SolarBodyId : u8
    {
        Sun, Moon, Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune
    };

    /// Compact universal object — fits in cache, sortable, renderable
    struct CelestialObject
    {
        // Identity
        ObjectType type;
        u64 id;                     // Unique ID (catalog number, seed, or generated)

        // Position (always J2000 equatorial)
        f64 ra;                     // radians
        f64 dec;                    // radians

        // Photometry
        f32 mag_v;                  // Visual magnitude
        f32 color_bv;               // B-V color index (or equivalent for non-stars)

        // Extended data (type-dependent, packed)
        union
        {
            struct  // Star, ProceduralStar
            {
                f32 parallax_mas;
                u8 spectral_type;
            } star;

            struct  // SolarSystemBody
            {
                SolarBodyId body_id;
                f32 distance_au;
                f32 angular_diameter_arcsec;
                f32 phase_illumination;     // 0..1
                f32 phase_angle_deg;
            } solar;

            struct  // DeepSkyObject
            {
                DsoSubType subtype;
                f32 size_arcmin;
            } dso;

            struct  // ProceduralGalaxy
            {
                u64 seed;
                f32 distance_mpc;
                f32 size_arcmin;
                u8 hubble_type;
            } galaxy;
        };

        // Display helpers
        [[nodiscard]] bool is_real() const;
        [[nodiscard]] bool is_procedural() const;
        [[nodiscard]] std::string get_type_name() const;
    };
}
```

### Universe Engine

```cpp
namespace parallax::universe
{
    class Universe
    {
    public:
        Universe();
        ~Universe();

        // === Initialization ===

        /// Load real catalogs (Hipparcos/Tycho-2, Messier, star names, constellations)
        void load_catalogs(const std::filesystem::path& data_dir);

        /// Initialize procedural generation with master seed
        void init_procedural(u64 master_seed);

        // === Per-frame update ===

        /// Update time-dependent objects (solar system, transients)
        /// Call once per frame before queries
        void update(f64 julian_date);

        // === Queries ===

        /// Query all objects visible in a sky cone
        /// Returns objects sorted by magnitude (brightest first)
        /// This is THE primary query — used by both skychart and imaging
        [[nodiscard]] std::vector<CelestialObject> query_fov(
            f64 ra_center,          // radians
            f64 dec_center,         // radians
            f64 radius_rad,         // cone radius
            f32 mag_limit,          // faintest magnitude
            QueryFlags flags = QueryFlags::All
        ) const;

        /// Query a single object by ID
        [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const;

        /// Search objects by name (for UI search bar, future)
        [[nodiscard]] std::vector<CelestialObject> search_name(
            const std::string& query, u32 max_results = 10
        ) const;

        // === Metadata ===

        /// Get common name for an object (if any)
        [[nodiscard]] std::optional<std::string> get_name(u64 id) const;

        /// Get constellation abbreviation for a position
        [[nodiscard]] std::string get_constellation(f64 ra_rad, f64 dec_rad) const;

        /// Get statistics
        [[nodiscard]] u64 get_real_object_count() const;
        [[nodiscard]] u64 get_procedural_estimate() const;

    private:
        // === Data providers (internal) ===

        std::unique_ptr<StarCatalogProvider> m_stars;
        std::unique_ptr<DsoCatalogProvider> m_dsos;
        std::unique_ptr<SolarSystemProvider> m_solar_system;
        std::unique_ptr<ProceduralProvider> m_procedural;

        // === Shared index ===
        std::unique_ptr<UnifiedSpatialIndex> m_spatial_index;

        // === Name database ===
        std::unordered_map<u64, std::string> m_names;          // Common names
        std::unordered_map<u64, std::string> m_designations;   // Catalog designations
    };

    /// Flags for query filtering
    enum class QueryFlags : u32
    {
        Stars           = 1 << 0,
        SolarSystem     = 1 << 1,
        DeepSky         = 1 << 2,
        Procedural      = 1 << 3,
        All             = 0xFFFFFFFF
    };
}
```

### Data Providers

Each data source implements a common provider interface:

```cpp
namespace parallax::universe
{
    class DataProvider
    {
    public:
        virtual ~DataProvider() = default;

        /// Query objects in a sky cone, append to results
        virtual void query_fov(
            f64 ra_center, f64 dec_center, f64 radius_rad,
            f32 mag_limit,
            std::vector<CelestialObject>& results
        ) const = 0;

        /// Query single object by ID (return nullopt if not owned by this provider)
        virtual std::optional<CelestialObject> query_object(u64 id) const = 0;

        /// Get total object count (estimate for procedural)
        virtual u64 get_count() const = 0;
    };
}
```

**Concrete providers:**

```
StarCatalogProvider
  - Wraps existing Tycho-2 data + SpatialIndex
  - Converts StarEntry → CelestialObject on query
  - Also keeps Hipparcos HIP IDs for constellation resolution
  - ID scheme: type_prefix | catalog_number

DsoCatalogProvider
  - Wraps existing Messier data (and future NGC/IC)
  - Converts DsoEntry → CelestialObject on query
  - ID scheme: type_prefix | catalog_number

SolarSystemProvider
  - Wraps existing SolarSystem ephemeris
  - Positions updated via update(jd)
  - 9 objects, always returned (no spatial culling needed)
  - ID scheme: type_prefix | body_id

ProceduralProvider
  - Seed-based deterministic generation
  - Spatial: HEALPix cells, each cell has a seed
  - Generated on demand when queried
  - Level-of-detail: more detail at smaller FOV
  - ID scheme: type_prefix | seed_hash
```

### ID Scheme

Every object gets a globally unique 64-bit ID:

```
Bits 63-56: type (8 bits) → ObjectType enum
Bits 55-0:  source-specific ID (56 bits)

For catalog objects:  type | catalog_number
For solar system:     type | body_id
For procedural:       type | seed_hash (deterministic from position + LOD)

This allows:
- Fast type checking: (id >> 56) gives ObjectType
- Stable IDs: same object always gets same ID
- No collisions between data sources
```

---

## Tasks

### Task 7.1 — Universe: Core Types and Interfaces

Define CelestialObject, ObjectType, DataProvider interface, QueryFlags, ID scheme.

**Files:**
- `src/universe/celestial_object.hpp`
- `src/universe/data_provider.hpp`
- `src/universe/object_id.hpp`

**Details:**
- CelestialObject struct with union for type-specific data
- DataProvider abstract base class
- ID encoding/decoding utilities
- QueryFlags enum with bitwise operators

Header-only where possible. No .cpp files needed for pure data types.

**Acceptance:** Compiles. Types are usable by subsequent tasks.

---

### Task 7.2 — Universe: Star Catalog Provider

Wrap existing star catalog + spatial index into a DataProvider.

**Files:** `src/universe/star_catalog_provider.hpp`, `src/universe/star_catalog_provider.cpp`

**Details:**
- Loads Tycho-2 CSV (existing code, refactored)
- Builds spatial index (existing code, moved here)
- Implements query_fov(): spatial query → convert StarEntry → CelestialObject
- Implements query_object(): lookup by ID
- Keeps Hipparcos HIP IDs accessible for constellation resolution
- ID: `(ObjectType::Star << 56) | hip_or_tyc_number`

**Key:** This is mostly moving existing catalog + spatial_index code into the provider pattern.
The existing `catalog_loader.cpp` and `spatial_index.cpp` become internals of this provider.

**Acceptance:** Provider returns correct star data. Same stars visible as before.

---

### Task 7.3 — Universe: DSO Provider

Wrap existing Messier catalog into a DataProvider.

**Files:** `src/universe/dso_provider.hpp`, `src/universe/dso_provider.cpp`

**Details:**
- Loads Messier CSV (existing code, refactored)
- Simple spatial query (110 objects, brute force is fine)
- Converts DsoEntry → CelestialObject with DsoSubType
- ID: `(ObjectType::DeepSkyObject << 56) | messier_number`

**Acceptance:** Provider returns correct Messier data.

---

### Task 7.4 — Universe: Solar System Provider

Wrap existing SolarSystem ephemeris into a DataProvider.

**Files:** `src/universe/solar_system_provider.hpp`, `src/universe/solar_system_provider.cpp`

**Details:**
- Calls SolarSystem::compute_all(jd) on update()
- Stores current positions for 9 bodies
- query_fov(): return all bodies above magnitude limit (always cheap, 9 objects)
- CelestialObject.solar union populated with distance, angular size, phase
- ID: `(ObjectType::SolarSystemBody << 56) | body_id`

**Acceptance:** Provider returns Sun, Moon, planets with correct positions and phase.

---

### Task 7.5 — Universe: Procedural Foundation

The first procedural provider — generates stars beyond catalog limits.

**Files:** `src/universe/procedural_provider.hpp`, `src/universe/procedural_provider.cpp`

**Details:**

This is where Parallax becomes infinite. The procedural provider generates
deterministic objects that don't exist in any catalog.

**Phase 1 approach (this sprint): Procedural background stars**

The Tycho-2 catalog covers stars down to ~mag 11.5.
The procedural provider fills in fainter stars using statistical models:
- Stellar density model based on galactic coordinates
- More stars toward galactic plane (Milky Way)
- Fewer stars toward galactic poles
- Luminosity function following Kroupa IMF

**Algorithm:**
```
1. Divide sky into HEALPix cells (nside=64 or 128)
2. For each cell in the query FOV:
   a. Compute galactic coordinates of cell center
   b. Compute expected star density (stars/deg² at given mag limit)
      density = base_density × galactic_model(l, b)
   c. Use cell_seed = hash(master_seed, healpix_pixel) for RNG
   d. Generate N = density × cell_area stars
   e. For each star:
      - Position: uniform random within cell
      - Magnitude: drawn from luminosity function (faint stars more common)
      - Color: drawn from color distribution (correlate with magnitude)
      - ID: hash(cell_seed, star_index) → deterministic, reproducible
3. Return generated objects as CelestialObject with type=ProceduralStar
```

**Galactic density model (simplified):**
```
density(l, b) = base × (1 + disk_factor × exp(-|b| / scale_height))
Where:
  base = 500 stars/deg² at mag 14
  disk_factor = 5.0 (galactic plane is 5× denser)
  scale_height = 10° (thin disk angular width)
  l = galactic longitude (not used in simplified model)
  b = galactic latitude
```

**Deterministic RNG:**
```cpp
// Use a fast, seedable hash function
// Given (master_seed, healpix_pixel, star_index) → always same star
// PCG or SplitMix64 are good choices
```

**Magnitude limit for procedural:**
Only generate procedural stars fainter than the catalog limit.
If Tycho-2 goes to mag ~11.5, procedural starts at mag 12.
This prevents doubles with catalog stars.

**Performance:**
Generation must be fast enough for real-time:
- Pre-generate and cache cells as they enter FOV
- Invalidate cache when FOV moves significantly
- Budget: < 2ms for procedural generation per frame

**Acceptance:**
- At MLIM > 12, procedural stars appear beyond Tycho-2 limit
- Milky Way region is noticeably denser than galactic poles
- Zooming in on empty regions reveals more faint stars (LOD)
- Stars are deterministic: same position + seed → same stars every time
- Performance stays ≥ 60fps

---

### Task 7.6 — Universe: Engine Assembly

Assemble the Universe class that owns all providers and handles queries.

**Files:** `src/universe/universe.hpp`, `src/universe/universe.cpp`

**Details:**
- Owns all four providers (star, DSO, solar system, procedural)
- load_catalogs(): loads all catalog data via providers
- init_procedural(): sets master seed, initializes procedural provider
- update(jd): updates solar system provider
- query_fov(): queries all providers, merges results, sorts by magnitude
- query_object(): tries each provider until found
- get_name(): lookup in name table
- get_constellation(): determine constellation from position

**Merge strategy for query_fov():**
```cpp
std::vector<CelestialObject> Universe::query_fov(...) const
{
    std::vector<CelestialObject> results;
    results.reserve(estimated_count);

    if (flags & QueryFlags::Stars)
        m_stars->query_fov(ra, dec, radius, mag_limit, results);

    if (flags & QueryFlags::DeepSky)
        m_dsos->query_fov(ra, dec, radius, mag_limit, results);

    if (flags & QueryFlags::SolarSystem)
        m_solar_system->query_fov(ra, dec, radius, mag_limit, results);

    if (flags & QueryFlags::Procedural && mag_limit > kProceduralMinMag)
        m_procedural->query_fov(ra, dec, radius, mag_limit, results);

    // Sort by magnitude (brightest first)
    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b) { return a.mag_v < b.mag_v; });

    return results;
}
```

**Name database:**
Load star_names.csv and Messier names into the name lookup table.
Planet names are hardcoded.

**Acceptance:**
- Universe::query_fov() returns stars, DSOs, planets, and procedural in one call
- All objects have correct IDs
- get_name() returns names for known objects
- Performance same as before (providers internally use spatial indexing)

---

### Task 7.7 — Refactor: Skychart Consumes Universe

Rewire the Application and all renderers to use Universe instead of separate data.

**Changes to:**
- `src/core/application.hpp/cpp` — replace separate catalogs with `Universe`
- `src/rendering/starfield.cpp` — consume `CelestialObject` instead of `StarEntry`
- `src/rendering/dso_renderer.cpp` — consume `CelestialObject` instead of `DsoEntry`
- `src/rendering/solar_system_renderer.cpp` — consume `CelestialObject`
- `src/ui/selection.cpp` — use `CelestialObject` for selected objects
- `src/ui/info_panel.cpp` — display from `CelestialObject`
- `src/overlay/constellations.cpp` — resolve HIP IDs via Universe

**New frame loop:**
```cpp
// 1. Input
// 2. Update time
m_universe->update(m_julian_date);

// 3. Query universe
auto objects = m_universe->query_fov(
    camera_ra, camera_dec,
    camera_fov_radius,
    camera_magnitude_limit
);

// 4. Transform and render
for (const auto& obj : objects)
{
    auto hz = equatorial_to_horizontal({obj.ra, obj.dec}, observer, lst);

    if (atmosphere_on && hz.alt < 0) continue;

    auto screen = horizontal_to_screen(hz, camera_pointing, camera_fov);
    if (!screen) continue;

    // Route to appropriate renderer based on type
    switch (obj.type)
    {
        case ObjectType::Star:
        case ObjectType::ProceduralStar:
            starfield_renderer.add_star(*screen, obj.mag_v, obj.color_bv);
            break;

        case ObjectType::SolarSystemBody:
            solar_renderer.add_body(*screen, obj);
            break;

        case ObjectType::DeepSkyObject:
            dso_renderer.add_dso(*screen, obj);
            break;

        // ... future types
    }
}

// 5. Render all
// (same render order as before)
```

**Remove from Application:**
- `m_star_catalog`, `m_spatial_index` → now inside StarCatalogProvider
- `m_messier_catalog` → now inside DsoCatalogProvider
- `m_solar_system_state` → now inside SolarSystemProvider

**Keep in Application:**
- `m_universe` (the single data source)
- All renderers (they don't change much, just input type changes)
- Camera, Input, UI, overlays

**Constellation resolution:**
Constellations need HIP IDs → star positions. Two options:
- A) Universe exposes a `resolve_hip(u32 hip) → optional<CelestialObject>` method
- B) StarCatalogProvider keeps a HIP lookup table, accessible via Universe

Use option A — cleaner interface.

**Selection refactor:**
SelectedObject becomes a CelestialObject + screen position.
Info panel reads all data from CelestialObject directly.
get_name() provides the display name.

**Acceptance (Sprint 07 Definition of Done):**
- [ ] All existing objects (stars, DSOs, solar system) visible — no regressions
- [ ] Single query_fov() returns all object types
- [ ] Procedural stars visible at MLIM > 12
- [ ] Milky Way region denser with procedural stars
- [ ] Procedural stars are deterministic (same seed → same stars)
- [ ] Object selection works for all types (star, planet, DSO, procedural)
- [ ] Info panel shows correct data for all types
- [ ] Constellation lines still work (HIP resolution via Universe)
- [ ] No more separate catalog members in Application
- [ ] Performance ≥ 60fps at MLIM 12 with procedural stars
- [ ] name lookup works for known objects
- [ ] No Vulkan validation errors

---

## Task Order

```
7.1 → 7.2 → 7.3 → 7.4 → 7.5 → 7.6 → 7.7
(types) (stars) (dso) (solar) (proc) (engine) (refactor)
```

---

## ID Registry

| Object Type       | ID Format (64-bit)                        | Example           |
|--------------------|-------------------------------------------|-------------------|
| Star               | `0x01` \| HIP/TYC number                 | `0x0100000000007CF5` (HIP 32005) |
| SolarSystemBody    | `0x02` \| body_id (0-8)                  | `0x0200000000000005` (Jupiter) |
| DeepSkyObject      | `0x03` \| Messier/NGC number             | `0x030000000000001F` (M31) |
| Exoplanet          | `0x04` \| host_star_id                   | future |
| ProceduralStar     | `0x10` \| seed_hash                      | `0x10A3F7C2...`  |
| ProceduralGalaxy   | `0x11` \| seed_hash                      | future |
| ProceduralNebula   | `0x12` \| seed_hash                      | future |

---

## Future Extensions (NOT this sprint)

This architecture naturally extends to:
- **NGC/IC catalog**: another DsoCatalogProvider, or extend existing one
- **Gaia DR3**: another StarCatalogProvider with streaming memory-mapped data
- **Exoplanet archive**: new provider type
- **Procedural galaxies**: extend ProceduralProvider with galaxy generation
- **Transient events**: new provider that generates time-dependent events
- **Imaging mode**: same query_fov() call, different renderer
- **Science mode**: query_object() for detailed analysis

Each extension is a new provider or an extension of an existing one.
The Universe query interface doesn't change.
