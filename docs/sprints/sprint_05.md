# Sprint 05 — Interactive UI

**Prerequisite:** Sprint 04 complete (constellations, grids, DSOs, Tycho-2)
**Goal:** Transform the skychart into an interactive tool with Stellarium-style UI, object selection, and clickable controls.
**Deliverable:** A fully interactive planetarium with toolbar, side panels, object info, and mouse-driven workflow.

---

## Overview

Sprint 05 replaces keyboard-only interaction with a proper UI.
The model is Stellarium: unobtrusive panels that appear on hover, toolbar at the bottom,
info panel on selection, all in the retro terminal aesthetic.

After this sprint, the skychart is a complete observational planning tool.

---

## Tasks

### Task 5.1 — UI: Panel System

Flexible panel/window system for the retro UI. All subsequent UI is built on this.

**Files:**
- `src/ui/panel_system.hpp`, `src/ui/panel_system.cpp`
- `shaders/ui_rect.vert`, `shaders/ui_rect.frag` (filled rectangles for backgrounds)

**Interface:**
```cpp
namespace parallax::ui
{
    struct PanelStyle
    {
        Vec4f background = {0.0f, 0.05f, 0.0f, 0.75f};   // Dark green-tinted transparent
        Vec4f border = {0.0f, 0.6f, 0.0f, 0.8f};          // Green border
        Vec4f text_color = {0.0f, 1.0f, 0.0f, 1.0f};      // Bright green
        Vec4f text_dim = {0.0f, 0.6f, 0.0f, 0.8f};        // Dim green labels
        Vec4f highlight = {0.0f, 1.0f, 0.0f, 0.3f};        // Button hover
        f32 padding = 8.0f;
        f32 border_width = 1.0f;
    };

    enum class PanelAnchor
    {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, Center, MiddleRight,
        BottomLeft, BottomCenter, BottomRight
    };

    class Panel
    {
    public:
        Panel(const std::string& id, PanelAnchor anchor, Vec2f size);

        void set_visible(bool visible);
        void set_draggable(bool draggable);

        /// Check if mouse is over this panel
        [[nodiscard]] bool contains(Vec2f mouse_pos) const;

        /// Get content area (inside padding)
        [[nodiscard]] Vec2f get_content_origin() const;
        [[nodiscard]] Vec2f get_content_size() const;

    protected:
        std::string m_id;
        PanelAnchor m_anchor;
        Vec2f m_position;       // Computed from anchor + window size
        Vec2f m_size;
        bool m_visible = true;
        bool m_draggable = false;
        PanelStyle m_style;
    };

    class PanelSystem
    {
    public:
        void init(vulkan::Context& context, VkRenderPass render_pass);
        void destroy();

        /// Register a panel
        void add_panel(std::unique_ptr<Panel> panel);

        /// Process input (hover, click, drag)
        void process_input(const core::Input& input, Vec2f mouse_pos);

        /// Render all panel backgrounds and borders
        void render_backgrounds(VkCommandBuffer cmd, VkExtent2D extent);

        /// Check if mouse is over any panel (to block sky interaction)
        [[nodiscard]] bool is_mouse_over_ui(Vec2f mouse_pos) const;

    private:
        std::vector<std::unique_ptr<Panel>> m_panels;
        // Vulkan: rect pipeline, vertex buffer for quads
    };
}
```

**Rect rendering:**
- Draw filled rectangles for panel backgrounds
- Draw line rectangles for borders
- All batched into minimal draw calls
- Semi-transparent backgrounds (alpha blend)

**Acceptance:** Can create positioned panels with retro green styling. Mouse over detection works.

---

### Task 5.2 — UI: Button & Widget System

Clickable buttons and basic widgets built on the panel system.

**Files:** `src/ui/widgets.hpp`, `src/ui/widgets.cpp`

**Widget types:**
```cpp
namespace parallax::ui
{
    class Button
    {
    public:
        Button(const std::string& label, Vec2f position, Vec2f size,
               std::function<void()> on_click);

        void update(Vec2f mouse_pos, bool mouse_clicked);
        void render(BitmapFont& font, LineRenderer& lines);

        [[nodiscard]] bool is_hovered() const;

    private:
        std::string m_label;
        Vec2f m_position, m_size;
        std::function<void()> m_on_click;
        bool m_hovered = false;
        bool m_pressed = false;
    };

    class ToggleButton : public Button
    {
    public:
        // Shows ON/OFF state visually
        [[nodiscard]] bool is_active() const;
        void set_active(bool active);
    private:
        bool m_active = false;
    };

    class Slider
    {
    public:
        Slider(const std::string& label, Vec2f position, f32 width,
               f32 min_val, f32 max_val, f32 step);

        void update(Vec2f mouse_pos, bool mouse_down);
        void render(BitmapFont& font, LineRenderer& lines);

        [[nodiscard]] f32 get_value() const;
        void set_value(f32 val);

    private:
        std::string m_label;
        f32 m_value, m_min, m_max, m_step;
        // ...
    };
}
```

**Visual style:**
- Buttons: green border, text centered, highlight on hover, brief flash on click
- Toggle buttons: filled background when active, outlined when inactive
- Sliders: horizontal track with draggable handle, value displayed
- All widgets use the BitmapFont for text — monospace, retro, pixel-sharp

**Acceptance:** Buttons click, toggle buttons show state, sliders drag. All in retro style.

---

### Task 5.3 — UI: Bottom Toolbar

Stellarium-style toolbar at the bottom of the screen.

**Files:** `src/ui/toolbar.hpp`, `src/ui/toolbar.cpp`

**Behavior:**
- Hidden by default, appears when mouse moves to bottom edge of screen
- Slides up with a quick animation (or instant appear)
- Disappears when mouse moves away (with short delay)

**Toolbar layout:**
```
┌─────────────────────────────────────────────────────────────────────────┐
│  [≡]  [★]  [✧]  [#]  [─]  │  [◀] [▶] [⏸] [×1] │  [🔍-] ═══●═══ [🔍+]  │
│  CONST STARS DSO  GRID HOR  │  REV  FWD PAUSE SPD │   FOV SLIDER          │
└─────────────────────────────────────────────────────────────────────────┘
```

**Button groups:**

Group 1 — Overlays:
| Button | Function            | Key equivalent |
|--------|---------------------|----------------|
| CONST  | Toggle constellations | C            |
| STARS  | Toggle star labels  | (new)          |
| DSO    | Toggle DSO layer    | D              |
| GRID   | Cycle grid mode     | G              |
| HORIZ  | Toggle horizon      | O              |

Group 2 — Time control:
| Button | Function            | Key equivalent |
|--------|---------------------|----------------|
| ◀      | Reverse time        | -              |
| ▶      | Forward time        | (speed up)     |
| ⏸      | Pause / Resume      | Space          |
| ×1     | Show/cycle speed    | 1-5            |

Group 3 — View:
| Button | Function            | Key equivalent |
|--------|---------------------|----------------|
| FOV-   | Zoom out            | Scroll down    |
| slider | FOV value           | Scroll         |
| FOV+   | Zoom in             | Scroll up      |

**Button rendering:**
Use text characters from the bitmap font as "icons" — keeps the retro aesthetic.
Each button is a small square with a character and a label below:
```
 ┌───┐
 │ ★ │
 └───┘
 CONST
```

**Acceptance:**
- Toolbar appears on mouse hover at bottom edge
- All buttons functional and equivalent to their keyboard shortcuts
- Toggle buttons show active/inactive state
- Time speed display updates
- FOV slider works
- Keyboard shortcuts still work alongside toolbar

---

### Task 5.4 — UI: Side Panel — Location & Time Settings

Left side panel for observer settings.

**Files:** `src/ui/side_panel.hpp`, `src/ui/side_panel.cpp`

**Behavior:**
- Appears when mouse moves to left edge, or via toolbar button
- Slides in from left

**Content:**
```
┌─── OBSERVER ──────────────┐
│                            │
│ Location                   │
│ ┌─ Preset ──────────────┐ │
│ │ ▸ La Palma            │ │
│ │   Mauna Kea           │ │
│ │   Paranal             │ │
│ │   McDonald            │ │
│ │   Custom...           │ │
│ └───────────────────────┘ │
│                            │
│ Latitude   +28° 45' 36" N │
│ Longitude  -17° 53' 24" W │
│ Elevation  2396 m          │
│                            │
│ ─── TIME ──────────────── │
│                            │
│ UTC  2026-03-01 19:14:55   │
│ LST  04h 41m 47s           │
│ JD   2461101.302            │
│                            │
│ Speed  [×1] [×10] [×100]  │
│         [×1k] [×10k]      │
│                            │
│ ─── SKY ───────────────── │
│                            │
│ Bortle  [1][2][3][4][5]   │
│         [6][7][8][9]      │
│ Current: 4                 │
│                            │
│ Mag Limit  ═══●═══  6.5   │
│                            │
└────────────────────────────┘
```

**Preset locations:**
```cpp
struct ObserverPreset
{
    std::string name;
    f64 latitude_deg;
    f64 longitude_deg;
    f64 elevation_m;
    f32 default_bortle;
};

// Built-in presets:
// La Palma (Roque de los Muchachos) — 28.76°N, 17.89°W, 2396m, Bortle 1
// Mauna Kea — 19.82°N, 155.47°W, 4205m, Bortle 1
// Paranal (ESO) — 24.63°S, 70.40°W, 2635m, Bortle 1
// McDonald Observatory — 30.67°N, 104.02°W, 2070m, Bortle 2
// Greenwich (urban) — 51.48°N, 0.00°, 0m, Bortle 8
```

**Acceptance:**
- Panel slides in from left
- Can switch observer location via preset buttons
- Sky rotates to correct position for new location
- Bortle scale adjustable via buttons
- Magnitude limit adjustable via slider
- Time speed selectable via buttons

---

### Task 5.5 — UI: Object Selection & Info Panel

Click on stars/DSOs to select them and display information.

**Files:**
- `src/ui/selection.hpp`, `src/ui/selection.cpp`
- `src/ui/info_panel.hpp`, `src/ui/info_panel.cpp`

**Selection system:**
```cpp
namespace parallax::ui
{
    struct SelectedObject
    {
        enum class Type { Star, DSO, None } type = Type::None;

        // Star data (if type == Star)
        catalog::StarEntry star;
        std::string star_name;          // Common name if available (e.g., "Sirius")

        // DSO data (if type == DSO)
        catalog::DsoEntry dso;

        // Common
        astro::EquatorialCoord equatorial;
        astro::HorizontalCoord horizontal;
        Vec2f screen_pos;
    };

    class Selection
    {
    public:
        /// Process a click at screen position
        /// Finds nearest star/DSO within click_radius pixels
        void process_click(Vec2f screen_pos,
                          const std::vector<StarVertex>& visible_stars,
                          const std::vector</* visible DSOs */>& visible_dsos,
                          f32 click_radius = 15.0f);

        /// Clear selection
        void clear();

        [[nodiscard]] const SelectedObject& get_selected() const;
        [[nodiscard]] bool has_selection() const;

    private:
        SelectedObject m_selected;
    };
}
```

**Hit testing:**
- On left click (when not dragging and not over UI):
  1. Find nearest star to click position (Euclidean distance in screen space)
  2. If within click_radius → select star
  3. Also check DSOs
  4. Prefer brighter objects if multiple candidates

**Visual indicator for selection:**
- Draw a targeting reticle around selected object (crosshair or circle)
- Color: bright yellow (`1.0, 0.9, 0.2`)
- Pulsing or blinking animation (optional)

**Info panel (right side):**
```
┌─── SELECTED ──────────────┐
│                            │
│ ★ SIRIUS (α CMa)          │
│ HIP 32349                  │
│                            │
│ RA    06h 45m 08.9s        │
│ Dec   -16° 42' 58.0"      │
│                            │
│ Alt   +32° 15' 44"        │
│ Az    195° 23' 12"        │
│                            │
│ Vmag    -1.46              │
│ B-V     +0.01              │
│ Spectral A1V               │
│                            │
│ Constellation  CMa         │
│                            │
│ ──────────────────────     │
│ [GOTO]  [TRACK]  [INFO]   │
│                            │
└────────────────────────────┘
```

GOTO button: functionality placeholder for Phase 2 (sends coordinates to telescope)
TRACK button: camera follows this object as time passes
INFO button: placeholder for extended info / external lookup

**Star names:**
Need a lookup table for common star names:
```csv
# data/catalogs/star_names.csv
HIP,Name,Bayer
32349,Sirius,α CMa
69673,Arcturus,α Boo
91262,Vega,α Lyr
...
```

~200-300 named stars cover all common names.

**Acceptance:**
- Click on star → selection indicator appears
- Info panel shows correct data
- Click on DSO → different info shown (designation, type, size)
- Click on empty sky → selection clears
- TRACK button makes camera follow object
- Selection works at all zoom levels
- Panel has retro green style

---

### Task 5.6 — UI: Mouse Interaction Refinement

Proper mouse handling: distinguish click vs drag, UI priority.

**Changes to:** `src/core/input.hpp/cpp`, Application

**Rules:**
```
Mouse interaction priority:
1. If mouse is over a UI panel → UI handles it (buttons, sliders)
2. If mouse clicks on sky → object selection
3. If mouse drags on sky → camera pan
4. Scroll on sky → camera zoom
5. Scroll on slider → slider value change
```

**Implementation:**
```cpp
// In frame loop:
bool mouse_over_ui = panel_system.is_mouse_over_ui(mouse_pos);

if (mouse_over_ui)
{
    panel_system.process_input(input, mouse_pos);
    // Do NOT pan camera or select objects
}
else
{
    if (input.was_clicked() && !input.was_drag())
    {
        selection.process_click(mouse_pos, ...);
    }
    if (input.is_mouse_dragging())
    {
        camera.pan(drag_delta);
    }
    if (input.get_scroll_delta() != 0)
    {
        camera.zoom(scroll_delta);
    }
}
```

**Click vs drag distinction:**
- Track mouse down position
- If mouse moves > 5 pixels before release → it's a drag
- If released within 5 pixels → it's a click

**Cursor changes (optional but nice):**
- Default: normal arrow
- Over UI button: hand pointer
- Dragging sky: closed hand
- Over star: crosshair

Use `SDL_SetCursor()` with `SDL_CreateSystemCursor()`.

**Acceptance:**
- Click selects objects, drag pans sky — never both at once
- UI buttons take priority over sky interaction
- Scrolling on sky zooms, scrolling on slider changes value
- Smooth, intuitive interaction

---

### Task 5.7 — Integration & Polish

Final wiring and visual polish.

**Updated render order:**
```
1. Sky background
2. Coordinate grid
3. Starfield
4. Constellation lines + labels
5. DSO icons + labels
6. Horizon + cardinals
7. Selection indicator
8. Panel backgrounds
9. Panel content (text, buttons, sliders)
10. HUD (if still visible separately, or merged into panels)
```

**HUD migration:**
The existing HUD (Sprint 03) can be:
- Kept as a minimal overlay for key info (FPS, MLIM, time)
- Or migrated entirely into the panel system

Recommendation: keep a minimal top bar with time and camera info,
move everything else to the side panels.

**Key bindings still work:**
All keyboard shortcuts from previous sprints remain functional.
The toolbar/panels are an ADDITION, not a replacement.

**Acceptance (Sprint 05 Definition of Done):**
- [ ] Bottom toolbar appears on mouse hover
- [ ] All overlay toggles work as toolbar buttons
- [ ] Time control buttons work (play, pause, speed, reverse)
- [ ] FOV slider in toolbar works
- [ ] Left panel with location presets, Bortle, magnitude limit
- [ ] Changing location rotates sky correctly
- [ ] Click on star → selection reticle + info panel (right side)
- [ ] Click on DSO → info panel with type, size, designation
- [ ] TRACK button follows selected object
- [ ] Mouse interaction priority correct (UI > selection > pan)
- [ ] Click vs drag properly distinguished
- [ ] All keyboard shortcuts still work
- [ ] Retro green terminal aesthetic consistent across all UI
- [ ] ≥ 60fps with all UI active
- [ ] No Vulkan validation errors

---

## Task Order

```
5.1 → 5.2 → 5.3 → 5.4 → 5.5 → 5.6 → 5.7
(panels) (widgets) (toolbar) (side) (select) (mouse) (polish)
```

---

## Visual Reference

The UI aesthetic is:
- **Stellarium** for layout and interaction model
- **Retro CRT terminal** for visual style
- Green phosphor on dark background
- Monospace bitmap font
- Thin green borders
- Semi-transparent panel backgrounds
- No rounded corners, no gradients, no modern UI trends
- Think: 1980s observatory control terminal meets modern UX
