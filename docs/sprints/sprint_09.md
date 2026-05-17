# Sprint 09 — UI Shell Refactor

**Prerequisite:** Sprint 08 complete (Knowledge System + Observation Sessions)
**Goal:** Replace the floating-panels UI with a persistent shell: fixed sidebar, top bar, status bar, central tab area with multi-pane split-screen and drag-resize.
**Deliverable:** A complete UI architecture refactor. The Planetarium becomes ONE tab among many. The lunar base becomes the game's default setting. Zero new astronomical features — pure restructuring.

---

## Scope Discipline

**This sprint adds NO new astronomical or instrument features.**
Every existing capability (planetarium, knowledge, sessions, archive, info panel)
gets reorganized into the new shell, but the underlying logic does not change.

If during implementation you realize a feature would be "nice to add" — don't.
Sprint 10 will add the first real instrument and imaging. Sprint 11 spectroscopy.
This sprint is the foundation that makes all of those reasonable to build.

---

## Architecture

### The Shell Hierarchy

```
┌──────────────────────────────────────────────────────────────────┐
│ TOP BAR (fixed height ~32px)                                      │
│ PARALLAX │ Moon Base: Tycho │ JD 2461109.45 │ Atmo ON │ FPS 60   │
├─────────┬────────────────────────────────────────────────────────┤
│         │                                                         │
│         │  ┌──────────────────┬──────────────────────────────┐   │
│ SIDEBAR │  │   TAB HEADER     │   TAB HEADER                 │   │
│ (fixed  │  │   [PLANETARIUM]  │   [IMAGING ×]                │   │
│  width  │  ├──────────────────┼──────────────────────────────┤   │
│  ~200px)│  │                  │                              │   │
│         │  │   TAB CONTENT    │   TAB CONTENT                │   │
│         │  │   (live render)  │   (live render)              │   │
│         │  │                  │                              │   │
│         │  │                  ├──────────────────────────────┤   │
│         │  │                  │   TAB HEADER                 │   │
│         │  │                  │   [ANALYSIS]                 │   │
│         │  │                  ├──────────────────────────────┤   │
│         │  │                  │   TAB CONTENT                │   │
│         │  └──────────────────┴──────────────────────────────┘   │
│         │                                                         │
├─────────┴────────────────────────────────────────────────────────┤
│ STATUS BAR (fixed height ~24px)                                   │
│ ⚙ 2 active sessions │ ✓ HIP 32005 b candidate │ Time x100 ▶      │
└──────────────────────────────────────────────────────────────────┘
```

### Tab Set (this Sprint)

All tabs are always available from the sidebar.
The four placeholder tabs show "Coming in Sprint XX" content.

| Tab           | Status         | Content                                         |
|---------------|----------------|--------------------------------------------------|
| PLANETARIUM   | Migrated       | Current skychart (was fullscreen)               |
| IMAGING       | Placeholder    | "Available with first telescope (Sprint 10)"    |
| SPECTROSCOPY  | Placeholder    | "Available with first spectrograph (Sprint 11)" |
| ANALYSIS      | Placeholder    | "Available with collected data (Sprint 10+)"    |
| ARCHIVE       | Migrated       | Was DataArchivePanel — now full tab             |
| ENCYCLOPEDIA  | New            | Knowledge DB browser, replaces InfoPanel as tab |
| ALL-SKY       | Placeholder    | "Available with all-sky camera (Sprint 10)"     |
| BASE          | New            | Tycho base management (instruments, status)     |

### Pane System (the split-screen mechanism)

The central area is a recursive layout of `Pane` objects:

```cpp
class Pane
{
    // A Pane is either:
    enum class Kind { Leaf, HorizontalSplit, VerticalSplit };
    Kind m_kind;

    // ...Leaf: holds one or more Tab views, one is active (showing)
    std::vector<TabId> m_tabs;
    u32 m_active_tab_index;

    // ...Split: holds two child panes with a draggable splitter
    std::unique_ptr<Pane> m_first;
    std::unique_ptr<Pane> m_second;
    f32 m_split_ratio;          // 0..1, position of splitter
};
```

The root pane starts as a single Leaf with PLANETARIUM as the only tab.

User operations:
- **Click a sidebar tab button** → if not in any pane, open in current focused leaf
- **Drag a tab header** to another pane's edge → splits that pane, places tab there
- **Drag a tab header** onto another tab area → moves tab there
- **Close a tab** (× button on header) → if last in its leaf, leaf collapses
- **Drag a splitter** → resizes both sides

This is a docking/tiling system, similar to VS Code or modern IDEs.

### Viewport Rendering

Each visible tab gets a `ViewportRect` representing its screen area:

```cpp
struct ViewportRect
{
    u32 x, y, width, height;    // pixels in window framebuffer
};
```

All existing renderers must be updated to render into a given viewport
instead of fullscreen. This affects:
- Sky background shader (uses viewport size for projection)
- Starfield renderer (viewport-relative screen coords)
- All overlay renderers (constellations, grid, horizon, DSOs, solar system)
- HUD renderer

The render order within a pane:
```
1. Clear pane region (scissor)
2. Set viewport to pane rect
3. Render tab content (planetarium, imaging, etc.)
4. Reset viewport to full window
```

Multiple visible panes mean multiple viewport renders per frame.
Performance budget: each pane should render in proportional time
to the existing fullscreen render (no big regression).

### State Persistence

Each tab is implemented as a `TabContent` object that lives for the
entire game session. When the tab is not visible:
- Its `update()` method is still called (it's "alive")
- Its `render()` method is NOT called
- It retains all internal state (scroll position, selection, filters, etc.)

This means a player can have Planetario panned to Orion, switch to Archive,
browse data, switch back to Planetario, and Orion is still in view.

### Multi-Location Observer System

The player operates from the **Moon Base at Tycho Crater** by default.
Instruments will eventually be at different locations (some on Moon, some on
Earth, some in orbit). This sprint introduces the foundation:

```cpp
struct ObserverLocation
{
    enum class Body { Earth, Moon, Mars, Space, ... };
    Body parent_body;

    f64 latitude_rad;
    f64 longitude_rad;
    f64 elevation_m;
    std::string name;       // "Tycho Crater Base", "La Palma", etc.
};

class ObserverRegistry
{
    // All locations available in the game
    std::vector<ObserverLocation> m_all_locations;

    // Currently active for the planetarium view
    u32 m_active_location_index;

    // Per-instrument: each instrument is at one location
    std::unordered_map<u64, u32> m_instrument_location;
};
```

**Tycho Crater coordinates (lunar latitude/longitude):**
- Latitude: -43.31° (south)
- Longitude: -11.36° (slightly western near side)
- Elevation: -1.2 km below mean lunar radius

The lunar observer:
- Sees the sky without atmospheric distortion
- Sees Earth as a celestial body (planetary phase, ~2° apparent diameter)
- Has a different "horizon" (lunar regolith is the obstruction)
- Sees the Sun + planets in slightly shifted positions (parallax negligible at most distances)

**For Sprint 09 the location math change is minimal:**
- Skychart projection works the same (RA/Dec → Alt/Az with different lat/lon)
- Earth becomes a new pseudo-solar-system body when observer is on Moon
- Atmosphere toggle: still exists for instruments on Earth, ignored for Moon instruments
- No need to implement Earth-as-celestial-body rendering this sprint
  (mark as placeholder for Sprint 10)

---

## Tasks

### Task 9.1 — Shell: Core Types and Layout

Foundation types for the shell, panes, tabs.

**Files:**
- `src/ui/shell/shell_types.hpp`
- `src/ui/shell/viewport_rect.hpp`
- `src/ui/shell/tab_id.hpp`

**Types:**
- `TabId` enum (Planetarium, Imaging, Spectroscopy, Analysis, Archive, Encyclopedia, AllSky, Base)
- `ViewportRect` struct (x, y, width, height in framebuffer pixels)
- `PaneKind` enum (Leaf, HorizontalSplit, VerticalSplit)
- `TabContent` abstract base class:
  ```cpp
  class TabContent
  {
  public:
      virtual ~TabContent() = default;
      virtual void update(f64 delta_time) = 0;
      virtual void render(VkCommandBuffer cmd, const ViewportRect& viewport) = 0;
      virtual void on_input(const InputEvent& event, const ViewportRect& viewport) = 0;
      virtual std::string get_display_name() const = 0;
      virtual TabId get_id() const = 0;
  };
  ```

Header-only where possible. Follow CLAUDE.md conventions. Update CMakeLists.

**Acceptance:** Types compile. Used by subsequent tasks.

---

### Task 9.2 — Shell: Pane Tree

The recursive pane structure with splits.

**Files:**
- `src/ui/shell/pane.hpp`
- `src/ui/shell/pane.cpp`
- `src/ui/shell/pane_tree.hpp`
- `src/ui/shell/pane_tree.cpp`

**Pane class:**
```cpp
class Pane
{
public:
    // Construction
    static std::unique_ptr<Pane> make_leaf(TabId initial_tab);
    static std::unique_ptr<Pane> make_split(
        PaneKind direction,
        std::unique_ptr<Pane> first,
        std::unique_ptr<Pane> second,
        f32 split_ratio = 0.5f);

    // Layout — compute viewport rects for all leaves
    void layout(const ViewportRect& available, 
                std::vector<std::pair<Pane*, ViewportRect>>& leaves_out);

    // Tab operations (only valid on leaf panes)
    void add_tab(TabId tab);
    void remove_tab(TabId tab);
    void set_active_tab(TabId tab);
    [[nodiscard]] TabId get_active_tab() const;
    [[nodiscard]] std::span<const TabId> get_tabs() const;

    // Split operations
    void split(PaneKind direction, TabId new_pane_tab, bool new_pane_first);
    void merge_with_child();   // When a child becomes empty

    // Splitter dragging
    void set_split_ratio(f32 ratio);

    [[nodiscard]] PaneKind get_kind() const;

    // ...
};
```

**PaneTree class:**
```cpp
class PaneTree
{
public:
    PaneTree();
    
    [[nodiscard]] Pane* get_root();
    
    // Find pane containing a tab
    [[nodiscard]] Pane* find_pane_for_tab(TabId tab);
    
    // Find leaf at screen position (for input routing)
    [[nodiscard]] Pane* find_leaf_at(Vec2f screen_pos, 
                                      const ViewportRect& root_viewport);
    
    // Find splitter at screen position (for drag detection)
    struct SplitterHit { Pane* pane; f32 perpendicular_offset; };
    [[nodiscard]] std::optional<SplitterHit> find_splitter_at(
        Vec2f screen_pos, const ViewportRect& root_viewport);
    
    // Compute layout for current frame
    void update_layout(const ViewportRect& root_viewport);
    
    // After layout, query the leaves and their viewports
    [[nodiscard]] std::span<const std::pair<Pane*, ViewportRect>> get_leaves() const;

private:
    std::unique_ptr<Pane> m_root;
    std::vector<std::pair<Pane*, ViewportRect>> m_cached_leaves;
};
```

**Splitter rendering and interaction:**
- Splitters drawn as 4px thick green lines between panes
- Hover state: brighter green when mouse is within 6px of splitter
- Drag: when clicked, update split_ratio of parent as mouse moves
- Constraint: ratio clamped to [0.1, 0.9] to prevent collapsing panes

**Acceptance:**
- Can build a tree with multiple splits
- Layout produces correct viewport rects for all leaves
- Splitter drag works smoothly
- find_leaf_at and find_splitter_at work for input routing

---

### Task 9.3 — Shell: Sidebar

Fixed left sidebar with instrument list, tab buttons, time controls.

**Files:**
- `src/ui/shell/sidebar.hpp`
- `src/ui/shell/sidebar.cpp`

**Sections (top to bottom):**

1. **Instruments** — list of player's instruments, click to focus, shows status
   ```
   INSTRUMENTS
   ─────────────
   ▣ Magic Inst.    [idle]
   ☐ (slot empty)
   ☐ (slot empty)
   ```
   For Sprint 09 there's only the Mock Instrument from Sprint 08.
   Status shows: idle, busy (observing), error.
   Clicking focuses the instrument (selection state, future use).

2. **Tabs** — buttons to open/focus each tab
   ```
   TABS
   ─────────────
   ▣ PLANETARIUM (active in some pane)
   ☐ IMAGING
   ☐ SPECTROSCOPY
   ☐ ANALYSIS
   ☐ ARCHIVE
   ☐ ENCYCLOPEDIA
   ☐ ALL-SKY
   ☐ BASE
   ```
   - Filled box if tab is currently displayed in any pane
   - Empty box if tab exists but is not currently visible
   - Click: open tab in the focused pane (or focused pane's free slot)
   - Right-click context menu: "Open in new split (right)", "Open in new split (bottom)"

3. **Time controls** — pause, time scale buttons (replaces existing time UI)
   ```
   TIME
   ─────────────
    ⏸  ▶  ⏵⏵
   x1  x10  x100  x1000  x10000
   ```

4. **Quick stats** (optional, bottom of sidebar)
   ```
   STATS
   ─────────────
   Known: 116,812 objects
   Discovered: 3
   Sessions: 1 active
   ```

**Visual style:**
- Background: dark green-tinted black (existing aesthetic)
- Width: 200 pixels (fixed)
- Bordered right edge (single green line)
- Section headers in dim green, content in bright green
- Monospace bitmap font (existing)

**Acceptance:**
- Sidebar always visible at left edge
- All sections render correctly
- Tab buttons open tabs in the focused pane
- Time controls work (replace existing)
- Instrument list shows mock instrument with status

---

### Task 9.4 — Shell: Top Bar and Status Bar

Persistent bars at the top and bottom of the screen.

**Files:**
- `src/ui/shell/top_bar.hpp`
- `src/ui/shell/top_bar.cpp`
- `src/ui/shell/status_bar.hpp`
- `src/ui/shell/status_bar.cpp`

**Top Bar (32px high):**
```
PARALLAX │ Moon Base: Tycho Crater │ JD 2461109.45 (2026-04-17 22:30 UTC) │ Atmo ON │ Bortle 1 (Moon)
```

Content from left to right:
- App name "PARALLAX" (constant)
- Active observer location name
- Julian date + civil time
- Atmosphere status (for Earth instruments; "N/A" or "Vacuum" on Moon)
- Bortle scale (for Earth; "Vacuum" or "Pristine" for Moon)

All read-only display. No interaction.

**Status Bar (24px high):**
```
⚙ 2 active sessions │ ✓ Last: HIP 32005 b candidate (3 min ago) │ Time x100 ▶
```

Content:
- Active session count with running indicator
- Most recent notification (rolling display of last 3-5)
- Current time scale and play/pause state
- FPS counter (right edge, dim)

Notifications appear here when:
- A session completes
- A new object is detected
- An object is confirmed
- Errors occur

Notifications fade after 30 seconds but remain in a popup history.

**Acceptance:**
- Top bar shows current observer info correctly
- JD updates in real-time
- Status bar shows active sessions and notifications
- Notifications appear when sessions complete

---

### Task 9.5 — Tab Content: Planetarium

Migrate the existing skychart into a TabContent.

**Files:**
- `src/ui/tabs/planetarium_tab.hpp`
- `src/ui/tabs/planetarium_tab.cpp`

**Migration steps:**
1. Move the existing skychart rendering code into `PlanetariumTab::render()`
2. Move skychart input handling into `PlanetariumTab::on_input()`
3. Make all rendering viewport-aware:
   - Sky background shader: use viewport size, not framebuffer size
   - Star coordinate projection: project relative to viewport center
   - Mouse input: subtract viewport origin from screen coords

**Viewport-aware rendering:**

The biggest technical challenge of this sprint. The existing renderers assume
they own the full framebuffer. Change them to render into a given viewport.

For Vulkan:
```cpp
void render(VkCommandBuffer cmd, const ViewportRect& vp)
{
    VkViewport vk_viewport = {
        .x = (f32)vp.x,
        .y = (f32)vp.y,
        .width = (f32)vp.width,
        .height = (f32)vp.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    VkRect2D scissor = {
        .offset = {(i32)vp.x, (i32)vp.y},
        .extent = {vp.width, vp.height}
    };
    
    vkCmdSetViewport(cmd, 0, 1, &vk_viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    
    // ... existing render code, but coordinates relative to viewport ...
}
```

For shaders that use screen-space (sky background, stars):
- Sky background fragment shader: replace `gl_FragCoord / window_size` with
  `(gl_FragCoord - viewport_origin) / viewport_size` (passed via push constant)
- Stars: their NDC coordinates [-1,1] still map correctly to the viewport
  via Vulkan's viewport transform — should "just work"

**Mouse input adjustment:**
```cpp
void on_input(const InputEvent& event, const ViewportRect& vp)
{
    InputEvent local = event;
    local.mouse_pos.x -= vp.x;
    local.mouse_pos.y -= vp.y;
    
    // Reject if outside viewport
    if (local.mouse_pos.x < 0 || local.mouse_pos.x > vp.width) return;
    if (local.mouse_pos.y < 0 || local.mouse_pos.y > vp.height) return;
    
    // Now handle as before with viewport-relative coordinates
}
```

**Existing skychart features to preserve:**
- All overlays (constellations, grid, horizon, DSOs, solar system)
- Object selection
- Time-dependent rotation
- All keyboard shortcuts (still work but only when planetarium pane is focused)
- Atmosphere toggle (still works, but irrelevant for lunar observer)

**Acceptance:**
- Planetarium renders correctly inside its viewport
- Resizing the pane reflows the projection (FOV adjusts to aspect ratio)
- Multiple Planetarium tabs can exist in different panes (each independent)
- Mouse interaction works correctly in any pane position
- All existing skychart features functional

---

### Task 9.6 — Tab Content: Archive

Migrate the data archive into a full-size tab.

**Files:**
- `src/ui/tabs/archive_tab.hpp`
- `src/ui/tabs/archive_tab.cpp`

The existing DataArchivePanel was a small floating panel.
Now it's a full tab with more breathing room.

**Layout (inside viewport):**
```
┌─────────────────────────────────────────────────────────────────┐
│ DATA ARCHIVE                                                    │
│ ─────────────────────────────────────────────────────────────  │
│                                                                  │
│ Filter: [All] [Photometry] [Spectroscopy] [Imaging] [Survey]    │
│ Sort:   [Date ▼] [Target] [SNR] [Type]                          │
│                                                                  │
│ ┌────────────────────────────────────────────────────────────┐ │
│ │ JD        TARGET           TECHNIQUE     SNR     SIZE      │ │
│ │ ──────────────────────────────────────────────────────     │ │
│ │ 2461104.5 HIP 32005        Photometry    35      1.2 KB    │ │
│ │ 2461103.2 HIP 54321        Spectroscopy  12      4.8 KB    │ │
│ │ 2461102.0 Sky survey       Survey         --     15 KB     │ │
│ │ ...                                                         │ │
│ └────────────────────────────────────────────────────────────┘ │
│                                                                  │
│ Selected record details:                                         │
│ ─────────────────────                                            │
│ Target: HIP 32005 b (procedural)                                 │
│ Date: 2026-04-17 22:30 UTC                                       │
│ Duration: 4.2 hours                                              │
│ Achieved SNR: 35.4                                               │
│                                                                  │
│ Measurements:                                                    │
│   period_days = 2.34 ± 0.05                                      │
│   transit_depth = 0.0023 ± 0.0001                                │
│                                                                  │
│ [EXPORT]  [DELETE]  [ANALYZE IN ANALYSIS TAB]                   │
└─────────────────────────────────────────────────────────────────┘
```

**Functionality:**
- Filter by data type
- Sort by date, target, SNR, type
- Click row to show details
- Export, Delete buttons (Export is placeholder for Sprint 11+)
- "Analyze" button is placeholder (Sprint 11+ analysis tools)

State to persist:
- Current filter
- Current sort
- Selected record ID
- Scroll position

**Acceptance:**
- Archive tab shows all records correctly
- Filter/sort work
- Record details display correctly
- State persists when tab is not visible

---

### Task 9.7 — Tab Content: Encyclopedia

New tab that replaces the floating InfoPanel.

**Files:**
- `src/ui/tabs/encyclopedia_tab.hpp`
- `src/ui/tabs/encyclopedia_tab.cpp`

The Encyclopedia is a browser for everything the player knows.
It replaces the floating info panel with a full-tab experience.

**Layout (inside viewport):**
```
┌─────────────────────────────────────────────────────────────────┐
│ ENCYCLOPEDIA                                                     │
│ ─────────────────────────────────────────────────────────────   │
│                                                                  │
│ ┌──────────────────────┬────────────────────────────────────┐  │
│ │ FILTER               │ HIP 32005 (Sirius)                 │  │
│ │ ────                 │ ────────                            │  │
│ │ Type:                │                                     │  │
│ │  ✓ Stars             │ ★ HISTORICAL [L3 - Characterized]   │  │
│ │  ✓ DSOs              │                                     │  │
│ │  ✓ Solar System      │ Designation: HIP 32349, α CMa      │  │
│ │  ✓ Discovered        │ Type: Star (A1V)                    │  │
│ │  ✗ Candidates        │                                     │  │
│ │                      │ Position                            │  │
│ │ Search:              │   RA  06h 45m 08.9s                 │  │
│ │ [             ]      │   Dec −16° 42′ 58.0″               │  │
│ │                      │                                     │  │
│ │ Sort: [Name]         │ Photometry                          │  │
│ │                      │   V mag: −1.46                      │  │
│ │ RESULTS (15,234)     │   B−V: +0.01                        │  │
│ │ ─────                │                                     │  │
│ │ ★ Sirius             │ Distance: 8.6 pc (parallax)        │  │
│ │   Procyon            │                                     │  │
│ │   Vega               │ Discovery details: HISTORICAL       │  │
│ │   Polaris            │ Known to humanity since antiquity   │  │
│ │   ...                │                                     │  │
│ │                      │ Properties to discover (L4+):       │  │
│ │   [scroll]           │   ? rotation_velocity_kms           │  │
│ │                      │   ? magnetic_field_gauss            │  │
│ │                      │   ? sub_universe (planets?)         │  │
│ │                      │                                     │  │
│ │                      │ Observations: 0 by you              │  │
│ │                      │                                     │  │
│ │                      │ [OBSERVE]  [LOCATE]  [TRACK]        │  │
│ └──────────────────────┴────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

**Two-pane internal layout:**
- Left (~30%): filter, search, results list
- Right (~70%): selected object details

**Filter options:**
- Type: Stars, DSOs, Solar System, Discovered (procedural), Candidates
- Knowledge level: minimum L1, L2, L3, L4+
- Search by name or designation

**Result list:**
- Sortable: by name, distance, magnitude, discovery date
- Indicator icons: ★ (historical), ✓ (confirmed discovery), ? (candidate)

**Selected object details (right pane):**
- Header: designation + status
- Known properties (organized by knowledge level)
- "?" markers for properties not yet measured (with hint of which level/technique unlocks them)
- Observation history (count, last date)
- Action buttons:
  - OBSERVE: opens Imaging tab with this target pre-selected (Sprint 10+ functional)
  - LOCATE: switches to Planetarium tab and centers on this object
  - TRACK: planetarium follows this object as time passes

**State to persist:**
- Active filters
- Search query
- Sort order
- Selected object ID
- Scroll position

**Acceptance:**
- Can browse all known objects
- Filter and search work
- LOCATE button switches to Planetarium and centers view
- Details show correct knowledge-filtered information

---

### Task 9.8 — Tab Content: Base

New tab for lunar base management.

**Files:**
- `src/ui/tabs/base_tab.hpp`
- `src/ui/tabs/base_tab.cpp`

The base tab shows the player's infrastructure at the lunar base
and (eventually) other locations.

**Layout for Sprint 09 (minimal but extensible):**
```
┌─────────────────────────────────────────────────────────────────┐
│ MOON BASE — TYCHO CRATER                                         │
│ ─────────────────────────────────────────────────────────────    │
│                                                                  │
│ LOCATION                                                         │
│   Tycho Crater (Lunar South-East)                                │
│   Lat: -43.31° Lon: -11.36° Elev: -1.2 km                        │
│   Lunar day: 14.7 days  Current local time: 14:32 (lunar)        │
│   Earth visible: Yes (phase: gibbous waxing, +90°)                │
│                                                                  │
│ INSTRUMENTS AT THIS LOCATION (1)                                 │
│ ──────────────                                                   │
│   ▣ Magic Instrument (mock)        [idle]                        │
│       Aperture: ∞   FOV: any   Filters: any                      │
│       Status: ready                                               │
│                                                                  │
│ AVAILABLE LOCATIONS                                              │
│ ──────────────                                                   │
│   ★ Moon - Tycho Crater (current)                                │
│   ○ Earth - La Palma (Roque de los Muchachos)                    │
│   ○ Earth - Mauna Kea                                            │
│   ○ Earth - Paranal                                              │
│                                                                  │
│ SYSTEMS                                                          │
│ ──────────────                                                   │
│   Power:        ████████████ 100% (Solar + RTG)                  │
│   Communication: ████████░░░░  Earth link nominal               │
│   Storage:      ███░░░░░░░░░  3% used (15 KB / 500 MB)           │
│   Crew status:  Operational                                       │
│                                                                  │
│ NETWORK                                                          │
│ ──────────────                                                   │
│   Connected observatories: 0 (Earth-Moon interferometry locked)  │
│   Unlock requirement: Build 2nd Moon instrument + Earth telescope│
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Sections:**
1. **Location** — current observer location info (read-only)
2. **Instruments at this location** — list of player's instruments here
3. **Available locations** — switch which location is "active" for planetarium
4. **Systems** — placeholder for resource management (power, comms, storage)
5. **Network** — placeholder for interferometric coordination (Sprint 12+)

For Sprint 09, all systems/network values are placeholders.
Power: always 100%. Storage: shows actual archive size.
The systems section gives the player a feeling of "there's more here later."

**Interactions:**
- Click an available location → switches active observer for Planetarium view
  (when player has instruments at multiple locations, this lets them switch view)
- Click an instrument → focuses it (selection state)
- All other elements are display-only this sprint

**State to persist:**
- Currently selected location for view (this could be global state, not tab-local)
- Selected instrument

**Acceptance:**
- Base tab shows lunar base info correctly
- Can switch between locations (when more than one is available)
- Existing observer system updates to lunar coordinates by default
- Skychart respects the active location

---

### Task 9.9 — Tab Content: Placeholders

Four placeholder tabs that show "coming in Sprint XX" content.

**Files:**
- `src/ui/tabs/imaging_tab.hpp` and `.cpp`
- `src/ui/tabs/spectroscopy_tab.hpp` and `.cpp`
- `src/ui/tabs/analysis_tab.hpp` and `.cpp`
- `src/ui/tabs/allsky_tab.hpp` and `.cpp`

Each tab is a simple TabContent implementation that:
- Renders a centered message
- Includes a thematic ASCII-art header
- Shows what sprint will activate the tab and what it will do

**Example (Imaging tab):**
```
              ╔═══════════════════════════════════╗
              ║      IMAGING                       ║
              ║                                    ║
              ║      ┌─[ ]─┐                       ║
              ║      │ ◯◯◯ │                       ║
              ║      └──┬──┘                       ║
              ║         │                          ║
              ║       ▲▲▲▲▲                        ║
              ║                                    ║
              ╚═══════════════════════════════════╝
                            
                Available in Sprint 10
                
   Live imaging feed from your active instrument.
   Shows what the telescope+sensor is capturing in real time.
   Save snapshots, monitor exposure progress, view bloom and PSF.
```

Each placeholder is minimal but communicates intent.
This gives the player a sense of what's coming.

**Acceptance:**
- Each placeholder renders correctly inside its viewport
- Centered content regardless of pane size
- Distinct ASCII art for each tab

---

### Task 9.10 — Observer System Refactor

Refactor observer location handling to support multiple locations.

**Files:**
- `src/astro/observer.hpp` (update)
- `src/astro/observer.cpp`
- `src/astro/observer_registry.hpp` (new)
- `src/astro/observer_registry.cpp`

**ObserverLocation** (extended from existing):
```cpp
enum class ParentBody { Earth, Moon, Mars, Sun, Space };

struct ObserverLocation
{
    std::string name;
    ParentBody parent_body;
    f64 latitude_rad;       // On parent body's surface
    f64 longitude_rad;
    f64 elevation_m;        // Above parent body's mean radius
    f32 bortle_scale;       // For Earth atmospheres; 0 for vacuum
    bool has_atmosphere;
};
```

**ObserverRegistry:**
```cpp
class ObserverRegistry
{
public:
    void add_location(const ObserverLocation& loc);
    
    [[nodiscard]] std::span<const ObserverLocation> get_all() const;
    
    [[nodiscard]] u32 get_active_index() const;
    void set_active(u32 index);
    [[nodiscard]] const ObserverLocation& get_active() const;
    
    // Built-in locations
    static ObserverRegistry create_default();
    
private:
    std::vector<ObserverLocation> m_locations;
    u32 m_active = 0;
};
```

**Default locations to add:**

```cpp
// Moon (default at game start)
{ "Tycho Crater Base", ParentBody::Moon, 
  rad(-43.31), rad(-11.36), -1200.0, 0.0f, false }

// Earth (existing presets, now part of the registry)
{ "La Palma (Roque)", ParentBody::Earth,
  rad(28.76), rad(-17.89), 2396.0, 1.0f, true }
{ "Mauna Kea", ParentBody::Earth,
  rad(19.82), rad(-155.47), 4205.0, 1.0f, true }
{ "Paranal", ParentBody::Earth,
  rad(-24.63), rad(-70.40), 2635.0, 1.0f, true }
{ "McDonald Observatory", ParentBody::Earth,
  rad(30.67), rad(-104.02), 2070.0, 2.0f, true }
```

**For locations on the Moon:**
- `has_atmosphere` = false
- `bortle_scale` = 0 (irrelevant, used as "Vacuum" indicator)
- The atmosphere toggle in skychart is automatically ignored / shown as N/A
- Horizon culling still applies (Moon surface blocks the sky below 0° alt)

**Sprint 09 simplifications (revisit in Sprint 10+):**
- LST and coordinate transforms still work as before — Alt/Az from RA/Dec at observer lat/lon
- We treat the Moon's rotation as locked to its 27.3-day sidereal period
  (technically tidal-locked; the math still works since we use sidereal time)
- We do NOT yet render the Earth as a celestial object from the Moon's viewpoint
  (mark as placeholder TODO in code, address in Sprint 10)
- The active observer is global state, not per-instrument

**Update existing code:**
- Replace hardcoded La Palma observer in Application with `ObserverRegistry`
- Default active location: Tycho Crater Base
- Skychart uses `m_observers.get_active()` for projection
- Base tab and Top bar read from `ObserverRegistry`

**Acceptance:**
- Game starts with observer at Tycho Crater
- Sky view is correctly computed for lunar latitude
- Can switch to Earth locations via Base tab
- Top bar reflects current location

---

### Task 9.11 — Shell Integration & Layout Manager

Wire the shell components together into the main Application.

**Files:**
- `src/ui/shell/shell.hpp`
- `src/ui/shell/shell.cpp`
- Update `src/core/application.hpp/cpp`

**Shell class:**
```cpp
class Shell
{
public:
    Shell();
    void init(vulkan::Context& ctx, VkRenderPass render_pass);
    void destroy();
    
    void update(f64 delta_time);
    void render(VkCommandBuffer cmd, VkExtent2D window_size);
    void on_input(const InputEvent& event);
    
    // Tab management
    void open_tab(TabId tab);            // Open in focused pane
    void open_tab_split(TabId tab, PaneKind direction);  // Split focused pane
    void close_tab(TabId tab);            // Close from wherever it is
    void focus_pane(Pane* pane);
    
    // Public hooks for other code
    [[nodiscard]] PlanetariumTab* get_planetarium_tab();
    [[nodiscard]] EncyclopediaTab* get_encyclopedia_tab();
    [[nodiscard]] BaseTab* get_base_tab();
    // ... etc

private:
    std::unique_ptr<Sidebar> m_sidebar;
    std::unique_ptr<TopBar> m_top_bar;
    std::unique_ptr<StatusBar> m_status_bar;
    std::unique_ptr<PaneTree> m_pane_tree;
    
    // All tabs are owned by Shell (alive for entire session)
    std::unordered_map<TabId, std::unique_ptr<TabContent>> m_tabs;
    
    Pane* m_focused_pane = nullptr;
};
```

**Application changes:**
- Remove all old floating panel members (InstrumentPanel, SessionsPanel, etc.)
- Add `std::unique_ptr<ui::shell::Shell> m_shell`
- Shell owns all tab content
- Application's frame loop becomes:
  ```cpp
  void Application::frame()
  {
      m_input.poll();
      m_shell->on_input(m_input.get_events());
      
      m_universe->update(m_julian_date);
      m_scheduler->update(m_julian_date, dt);
      // ... harvest sessions, update knowledge ...
      
      m_shell->update(dt);
      
      auto cmd = begin_frame();
      m_shell->render(cmd, m_window_size);
      end_frame(cmd);
  }
  ```

**Default layout at startup:**
- Single root leaf with Planetarium tab active
- Shell shows: Sidebar (left), Top bar (top), Status bar (bottom)
- Center area is the Planetarium tab fullscreen

**Persistence:**
- Save current pane tree layout to a JSON file at exit
- Restore on next launch
- File: `user_data/shell_layout.json`
- If load fails or file missing: use default layout

**Acceptance (Sprint 09 Definition of Done):**
- [ ] Old floating panels are GONE — sidebar/topbar/statusbar shell is in place
- [ ] Planetarium renders correctly as a tab, including all overlays
- [ ] Can resize panes by dragging splitters
- [ ] Can have multiple panes visible side by side
- [ ] Can switch the active tab in each pane
- [ ] Tab state persists when switching away and back
- [ ] All 8 tabs are accessible from sidebar
- [ ] Placeholder tabs show clear "Coming in Sprint X" messages
- [ ] Sidebar shows instruments, tab list, time controls
- [ ] Top bar shows location, JD, time
- [ ] Status bar shows sessions and notifications
- [ ] Base tab shows lunar base info, can switch observer location
- [ ] Encyclopedia tab fully replaces old InfoPanel
- [ ] Archive tab fully functional
- [ ] Observer defaults to Tycho Crater on Moon
- [ ] Layout persists across game sessions
- [ ] All previous functionality (selection, time control, atmosphere toggle, observations) still works
- [ ] ≥ 60fps with all features active
- [ ] No Vulkan validation errors

---

## Task Order

```
9.1 → 9.2 → 9.3 → 9.4 → 9.5 → 9.6 → 9.7 → 9.8 → 9.9 → 9.10 → 9.11
types  panes sidebar bars planet archive ency base placeh observer integrate
```

The critical path:
- Types and panes first (foundation)
- Sidebar/bars next (chrome around the central area)
- Planetarium migration is the biggest single task (Task 9.5)
- After Planetarium works in its viewport, other tabs follow the same pattern
- Observer refactor (9.10) before final integration (9.11)
- Integration ties everything together

---

## Risk Areas

### 1. Viewport-aware rendering (Task 9.5)
The biggest technical risk. The existing renderers assume fullscreen.
Sprint 09 must make them viewport-aware without breaking anything.

**Mitigation strategy:**
- Keep the OLD fullscreen render code path working initially
- Add the viewport-aware path side by side
- Test viewport rendering in a single isolated test (a single pane filling the screen)
- Only after that works perfectly, enable multi-pane layouts
- Have a debug command to force "old fullscreen mode" for comparison

### 2. Tab input routing (Task 9.11)
When the screen has multiple panes, input must go to the right tab.

**Mitigation:**
- Always determine which pane the mouse is in BEFORE processing the event
- Use the focused pane for keyboard input
- Sidebar/bars always receive input first if mouse is over them
- Status bar and Top bar do not consume keyboard input

### 3. Performance with multiple panes (general)
Rendering 4 simultaneously visible planetariums at 60fps is asking a lot.

**Mitigation:**
- Magnitude limit per pane (less detailed in smaller panes)
- Frame skip for non-focused panes (render every other frame)
- Optimize the universe query: cache results across panes if same location/time
- Profile early in Sprint 09 to catch regressions

### 4. Layout persistence
Saving and restoring a recursive tree structure needs care.

**Mitigation:**
- Use a simple JSON schema with type discriminators
- Validate on load: if structure is malformed, fall back to default
- Don't crash if a tab type is no longer recognized (skip it)

---

## What This Sprint Does NOT Do

To prevent scope creep, here is what is explicitly OUT of scope for Sprint 09:

- ❌ Real imaging (Sprint 10)
- ❌ Real spectroscopy (Sprint 11)
- ❌ Real analysis tools (Sprint 10+)
- ❌ Earth as a celestial body from the Moon (Sprint 10)
- ❌ Multiple instruments at different locations (Sprint 10+)
- ❌ Interferometry (Sprint 12+)
- ❌ All-sky camera (Sprint 10)
- ❌ Lunar terrain/horizon obstruction model (Sprint 10+)
- ❌ Earth-Moon communication delays (Sprint 12+)
- ❌ Base infrastructure mechanics (power, storage limits) (Sprint 12+)

The Mock Instrument from Sprint 08 is still the only instrument.
Sprint 10 will replace it with a real telescope + CCD.

---

## Notes for Implementation

### Splitter UX
The drag-resize splitters are subtle but critical for feel.
- Hover region should be ~6px wide (easier to grab than the visible 4px line)
- Cursor should change to resize-cursor when hovering
- Constraint: panes can't be resized below 100px in their split dimension
- Visual feedback during drag: line follows mouse instantly, panes resize after release

### Tab Drag-and-Drop (optional polish)
If time allows, implement drag-and-drop of tab headers:
- Drag a tab header → onto another pane's edge → splits that pane
- Drag a tab header → onto another tab area → moves the tab

This is "nice to have" — if it's hard, skip it for now. The right-click context
menu on sidebar tab buttons (Open in new split right/bottom) is sufficient.

### Consistency
All tabs must use the same visual aesthetic:
- Dark green-tinted black background
- Bright green text (#00FF00) for content
- Dim green (#00AA00) for labels
- Thin green borders (1px) where needed
- Bitmap font (existing) for all text
- No anti-aliasing, no rounded corners

The result should look like a NASA mission control terminal from 1985,
not a modern web app.
