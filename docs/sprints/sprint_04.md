# Sprint 04 — Catalogs & Sky Overlays

**Prerequisite:** Sprint 03 complete (skychart with Hipparcos, sky background, HUD)
**Goal:** Enrich the skychart with constellation overlays, coordinate grids, deep sky objects, and the Tycho-2 catalog.
**Deliverable:** A complete astronomical atlas with constellations, Messier objects, coordinate grids, cardinal markers, and 2.5M stars.

---

## Overview

Sprint 04 adds the informational layers that turn a starfield into a usable skychart.
After this sprint, the planetarium shows everything an observer needs to identify
and locate celestial objects — constellations, deep sky targets, coordinate references.

---

## Tasks

### Task 4.1 — Rendering: Line Renderer

General-purpose GPU line renderer for constellations, grids, and horizon markers.
Reused across multiple overlay systems.

**Files:**
- `src/rendering/line_renderer.hpp`, `src/rendering/line_renderer.cpp`
- `shaders/line.vert`, `shaders/line.frag`

**Interface:**
```cpp
namespace parallax::rendering
{
    struct LineVertex
    {
        Vec2f position;     // Screen NDC
        Vec4f color;        // RGBA
    };

    class LineRenderer
    {
    public:
        void init(vulkan::Context& context, VkRenderPass render_pass);
        void destroy();

        /// Clear all lines for this frame
        void begin_frame();

        /// Add a line segment (screen coordinates)
        void add_line(Vec2f from, Vec2f to, Vec4f color, f32 thickness = 1.0f);

        /// Add a line strip (connected points)
        void add_line_strip(std::span<const Vec2f> points, Vec4f color, f32 thickness = 1.0f);

        /// Add a circle (approximated as line strip)
        void add_circle(Vec2f center, f32 radius, Vec4f color,
                       u32 segments = 32, f32 thickness = 1.0f);

        /// Render all queued lines
        void render(VkCommandBuffer cmd);

    private:
        std::vector<LineVertex> m_vertices;
        // Vulkan: pipeline (LINE_LIST topology), vertex buffer
    };
}
```

**Pipeline:**
- Topology: `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`
- Blending: alpha blend
- Line width: 1.0 (Vulkan wide lines require device feature, start with 1.0)
- Dynamic state: viewport, scissor
- Vertex input: position (vec2) + color (vec4)
- Rendered AFTER starfield, BEFORE HUD

**Acceptance:** Can draw colored lines on screen. Lines are crisp and correctly positioned.

---

### Task 4.2 — Overlay: Constellation Lines

The 88 IAU constellations rendered as stick figures connecting stars.

**Files:**
- `src/overlay/constellations.hpp`, `src/overlay/constellations.cpp`
- `data/catalogs/constellation_lines.csv`

**Data format:**
```csv
# constellation_lines.csv
# Each line: constellation abbreviation, HIP star 1, HIP star 2
# One line segment per row
UMa,54061,53910
UMa,53910,58001
UMa,58001,59774
UMa,59774,62956
Ori,27989,26727
Ori,26727,25336
...
```

Source: The constellation line data is well-established. Use IAU standard asterisms.
Approximately 800-1000 line segments total for all 88 constellations.

**Interface:**
```cpp
namespace parallax::overlay
{
    struct ConstellationData
    {
        std::string abbreviation;   // "Ori", "UMa", etc.
        std::string name;           // "Orion", "Ursa Major", etc.
        std::vector<std::pair<u32, u32>> segments;  // HIP ID pairs
    };

    class Constellations
    {
    public:
        /// Load constellation line data
        void load(const std::filesystem::path& lines_path,
                  const std::filesystem::path& names_path);

        /// Resolve HIP IDs to star positions from loaded catalog
        void resolve_stars(const std::vector<catalog::StarEntry>& catalog);

        /// Generate lines for current view
        void update(const rendering::Camera& camera,
                   const astro::ObserverLocation& observer,
                   f64 lst_rad,
                   rendering::LineRenderer& lines,
                   rendering::BitmapFont& font);

        void set_visible(bool visible);
        [[nodiscard]] bool is_visible() const;

    private:
        std::vector<ConstellationData> m_constellations;
        bool m_visible = true;

        // Preresolved: HIP ID → index in star catalog
        std::unordered_map<u32, u32> m_hip_to_index;
    };
}
```

**Constellation names data:**
```csv
# constellation_names.csv
UMa,Ursa Major
UMi,Ursa Minor
Ori,Orion
Sco,Scorpius
...
```

**Rendering:**
1. For each constellation:
   a. For each segment (HIP1, HIP2):
      - Look up both stars' RA/Dec
      - Transform both to Alt/Az → screen coords
      - Skip segment if either star is below horizon
      - Skip segment if both stars are off screen
      - Draw line between the two screen positions
   b. Compute constellation center (average of star positions on screen)
   c. Draw constellation abbreviation label at center

**Color:** Dim blue-gray (`0.3, 0.4, 0.6, 0.5`) — visible but not distracting.
**Labels:** Same color, small text, abbreviation only (e.g., "ORI", "UMA").

**Toggle:** `C` key toggles constellation lines on/off.

**Acceptance:**
- Major constellations clearly recognizable (Orion, Big Dipper, Scorpius)
- Lines connect correct stars
- Labels centered on each constellation
- Lines clip properly at horizon
- Toggleable with C key

---

### Task 4.3 — Overlay: Coordinate Grid

Equatorial and Alt/Az grid overlays.

**Files:** `src/overlay/coord_grid.hpp`, `src/overlay/coord_grid.cpp`

**Interface:**
```cpp
namespace parallax::overlay
{
    enum class GridType
    {
        None,
        Equatorial,     // RA/Dec lines
        AltAzimuth,     // Alt/Az lines
        Both
    };

    class CoordGrid
    {
    public:
        void update(const rendering::Camera& camera,
                   const astro::ObserverLocation& observer,
                   f64 lst_rad,
                   GridType type,
                   rendering::LineRenderer& lines,
                   rendering::BitmapFont& font);

        void set_type(GridType type);
        [[nodiscard]] GridType get_type() const;
        void cycle_type();  // None → Eq → AltAz → Both → None

    private:
        GridType m_type = GridType::None;

        void draw_equatorial_grid(/* params */);
        void draw_altaz_grid(/* params */);
    };
}
```

**Equatorial grid:**
- RA lines every 1h (15°): 24 lines, great circles from pole to pole
- Dec lines every 10°: 18 lines, small circles parallel to celestial equator
- Label RA at Dec=0° crossing: "0h", "1h", ..., "23h"
- Label Dec at RA=0 crossing: "+90°", "+80°", ..., "-90°"
- Color: dim red-orange (`0.6, 0.3, 0.2, 0.3`)
- Celestial equator (Dec=0°) slightly brighter

**Alt/Az grid:**
- Azimuth lines every 15°: 24 lines, vertical from zenith to horizon
- Altitude lines every 10°: 9 lines (0° to 80°), concentric around zenith
- Label at horizon: degree values
- Color: dim green (`0.2, 0.5, 0.2, 0.3`)
- Horizon line (Alt=0°) slightly brighter

**Drawing curved lines on screen:**
Each grid line is a great circle or small circle on the sphere.
Approximate by sampling points along the curve:
```
For a Dec = 30° line:
  for ra = 0 to 2π step small_increment:
    point = equatorial_to_horizontal(ra, dec=30°) → screen
  Connect sequential points as line strip
  Skip segments that cross the horizon or go off screen
```

Use ~72 points per line (every 5°) for smooth curves.

**Toggle:** `G` key cycles through grid types (None → Eq → AltAz → Both → None).

**Acceptance:**
- Equatorial grid lines curve correctly across the sky
- Alt/Az grid is centered on zenith
- Grid labels are readable
- Both grids can be displayed simultaneously
- Smooth curves, no visible segmentation at 60° FOV

---

### Task 4.4 — Overlay: Horizon & Cardinal Markers

Clear horizon line with cardinal direction labels.

**Files:** `src/overlay/horizon.hpp`, `src/overlay/horizon.cpp`

**Implementation:**
Draw a continuous line at Alt = 0° across the full azimuth range.
```
For az = 0 to 2π step small_increment:
  point = horizontal_to_screen({alt=0, az})
  Add to line strip
```

**Cardinal markers:**
At N (0°), NE (45°), E (90°), SE (135°), S (180°), SW (225°), W (270°), NW (315°):
- Draw a small tick mark above the horizon line
- Draw the label ("N", "NE", "E", ...) above the tick

**Color:**
- Horizon line: warm brown (`0.5, 0.3, 0.2, 0.6`)
- Cardinal labels: brighter (`0.7, 0.5, 0.3, 0.8`)
- N label: highlighted red (`1.0, 0.3, 0.2, 0.8`) — traditional compass convention

**Toggle:** Part of the grid system — visible when any grid is active, or always visible.
Can be a separate toggle with `O` key (horizon Overlay).

**Acceptance:**
- Horizon line visible as a continuous line
- Cardinal directions correctly placed (N at az=0, E at az=90)
- N is highlighted
- Labels readable
- Line follows the curved horizon as camera pans

---

### Task 4.5 — Catalog: Messier Objects

Load and display the 110 Messier objects as schematic icons.

**Files:**
- `src/catalog/dso_entry.hpp`
- `src/catalog/dso_loader.hpp`, `src/catalog/dso_loader.cpp`
- `src/rendering/dso_renderer.hpp`, `src/rendering/dso_renderer.cpp`
- `data/catalogs/messier.csv`

**DSO Entry:**
```cpp
namespace parallax::catalog
{
    enum class DsoType : u8
    {
        Galaxy,
        Nebula,             // Emission, reflection, planetary
        OpenCluster,
        GlobularCluster,
        SupernovaRemnant,
        Other
    };

    struct DsoEntry
    {
        std::string designation;    // "M1", "M31", "M42"
        std::string common_name;    // "Crab Nebula", "Andromeda Galaxy"
        f64 ra;                     // radians
        f64 dec;                    // radians
        f32 mag_v;                  // Visual magnitude
        f32 size_arcmin;            // Apparent size (major axis)
        DsoType type;
    };
}
```

**Messier catalog CSV:**
```csv
Designation,Name,RA_deg,Dec_deg,Vmag,Size_arcmin,Type
M1,Crab Nebula,83.633,22.014,8.4,6.0,SupernovaRemnant
M13,Hercules Cluster,250.423,36.461,5.8,20.0,GlobularCluster
M31,Andromeda Galaxy,10.685,41.269,3.4,178.0,Galaxy
M42,Orion Nebula,83.822,-5.391,4.0,85.0,Nebula
M45,Pleiades,56.601,24.115,1.6,110.0,OpenCluster
...
```

All 110 Messier objects with accurate positions, magnitudes, sizes, and types.

**Rendering — Schematic Icons:**
Draw using the LineRenderer (no textures needed):

| Type              | Icon                                          |
|-------------------|-----------------------------------------------|
| Galaxy            | Ellipse (tilted)                              |
| Nebula            | Square                                        |
| OpenCluster       | Circle with dots (dashed circle)              |
| GlobularCluster   | Circle with cross (+)                         |
| SupernovaRemnant  | Circle                                        |

Icon size on screen: fixed size (e.g., 12px) or scaled by apparent size if zoomed in.

**Color:** Magenta/pink (`0.8, 0.4, 0.6, 0.7`) — distinct from stars and grid lines.

**Labels:** "M31", "M42" drawn next to each icon.

**Visibility:** Controlled by magnitude limit (same slider as stars).
M31 at mag 3.4 visible at MLIM 6.5, M1 at mag 8.4 needs MLIM > 8.4.

**Toggle:** `D` key toggles DSO visibility.

**Acceptance:**
- All 110 Messier objects at correct positions
- Schematic icons distinguishable by type
- Labels readable
- M42 visible near Orion's sword
- M31 visible in Andromeda
- Magnitude filtering works

---

### Task 4.6 — Catalog: Tycho-2 Upgrade

Upgrade from Hipparcos (~118k) to Tycho-2 (~2.5 million stars).

**Files:**
- `tools/prepare_tycho2.py`
- Updated `src/catalog/catalog_loader.cpp`
- New: `src/catalog/spatial_index.hpp`, `src/catalog/spatial_index.cpp`

**Why spatial indexing is now required:**
With 2.5M stars, iterating all per frame is not feasible even with the prefilter.
At FOV 60°, only ~1/6 of the sky is visible → need to query only that region.

**Simple Spatial Index (for Sprint 04):**
Divide the sky into zones by declination bands:

```cpp
namespace parallax::catalog
{
    class SpatialIndex
    {
    public:
        /// Build index from star catalog, dividing into dec bands
        void build(const std::vector<StarEntry>& stars, u32 num_bands = 180);

        /// Query all stars in a sky region
        /// Returns indices into the original star vector
        [[nodiscard]] std::vector<u32> query(
            f64 ra_center, f64 dec_center,
            f64 radius_rad,
            f32 mag_limit
        ) const;

    private:
        struct Band
        {
            f64 dec_min, dec_max;
            std::vector<u32> star_indices;  // Sorted by RA within band
        };
        std::vector<Band> m_bands;
    };
}
```

**Declination band approach:**
- 180 bands (1° each) from -90° to +90°
- Stars sorted by RA within each band
- Query: determine which bands overlap with FOV, binary search RA range within each
- O(bands_in_fov × log(stars_per_band)) instead of O(all_stars)

This is simpler than full HEALPix but sufficient for 2.5M stars.
HEALPix indexing comes with the binary .plxcat format in a later sprint.

**Tycho-2 preprocessing:**
```python
# tools/prepare_tycho2.py
# Input: tyc2.dat (Tycho-2 main catalog)
# Output: data/catalogs/tycho2.csv
# Columns: TYC,RA_deg,Dec_deg,VTmag,BV
# Filter: valid positions and magnitudes only
# Note: Tycho uses VT magnitude, convert to Johnson V:
#   V = VT - 0.090 × (BT - VT)   [approximate]
```

**Performance target:**
- Full catalog load: < 2 seconds
- Per-frame query (FOV 60°): < 5ms
- Visible stars at MLIM 6.5: ~9,000 (same as Hipparcos naked-eye)
- Visible stars at MLIM 10: ~200,000+
- Maintain ≥ 60fps at MLIM 10 with 60° FOV

**Acceptance:**
- 2.5M stars loaded
- Spatial index query fast enough for 60fps
- Dense star regions clearly visible when MLIM increased
- No visible difference from Hipparcos at MLIM 6.5 (same stars, same brightness)
- Performance logged per frame

---

### Task 4.7 — Key Bindings Update & Integration

Wire all new overlays into the application.

**New key bindings:**
- `C` — Toggle constellation lines + labels
- `G` — Cycle coordinate grid (None → Eq → AltAz → Both)
- `D` — Toggle deep sky objects (Messier)
- `O` — Toggle horizon overlay + cardinal markers
- `[` / `]` — Magnitude limit (from Sprint 03)
- All existing keys remain

**Updated HUD:**
Add overlay status indicators to HUD (bottom-left or as a status bar):
```
CONST  ON    GRID  EQ    DSO  ON    HORIZ  ON
```
Small status line showing which overlays are active.

**Updated render order:**
```
1. Sky background
2. Coordinate grid lines (behind stars)
3. Starfield (additive)
4. Constellation lines (over stars)
5. DSO icons (over stars)
6. Horizon line + cardinal markers (over everything except HUD)
7. Constellation labels + DSO labels + grid labels
8. HUD (always on top)
```

**Acceptance (Sprint 04 Definition of Done):**
- [ ] Constellation lines for all 88 IAU constellations
- [ ] Constellation labels (abbreviations) centered
- [ ] Equatorial coordinate grid with RA/Dec labels
- [ ] Alt/Az coordinate grid with altitude/azimuth labels
- [ ] Grids cycle with G key
- [ ] Horizon line with N/E/S/W/NE/SE/SW/NW cardinal markers
- [ ] N marker highlighted in red
- [ ] 110 Messier objects with schematic type icons
- [ ] Messier labels (M31, M42, etc.)
- [ ] Tycho-2 catalog loaded (~2.5M stars)
- [ ] Spatial index keeps frame time < 16ms
- [ ] All overlays toggleable independently
- [ ] HUD shows overlay status
- [ ] ≥ 60fps at FOV 60°, MLIM 6.5 with all overlays on
- [ ] No Vulkan validation errors

---

## Task Order

```
4.1 → 4.2 → 4.3 → 4.4 → 4.5 → 4.6 → 4.7
(lines) (const) (grid) (horizon) (messier) (tycho2) (integrate)
```

---

## Data Sources

| Data                   | Source                                        | Format          |
|------------------------|-----------------------------------------------|-----------------|
| Constellation lines    | IAU standard asterisms / Stellarium data      | CSV (HIP pairs) |
| Constellation names    | IAU list of 88 constellations                 | CSV             |
| Messier catalog        | SEDS Messier database                         | CSV             |
| Tycho-2 catalog        | CDS / ESA Tycho-2 archive                     | Fixed-width     |

If direct download is blocked, the constellation and Messier data can be
embedded directly in the source code as constexpr arrays — they're small enough.
Tycho-2 requires the preprocessing script and the actual data file.
