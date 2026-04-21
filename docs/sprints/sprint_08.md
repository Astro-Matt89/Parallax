# Sprint 08 — Knowledge System & Observation Sessions

**Prerequisite:** Sprint 07 complete (Universe Engine with unified query interface)
**Goal:** Lay the architectural foundations for the discovery-driven gameplay: stratified knowledge, observation sessions with real-time progression, historical vs player-discovered distinction, sub-universe hierarchy.
**Deliverable:** An end-to-end vertical slice: a mock instrument can schedule an observation of a procedural object, time advances, SNR accumulates, data is produced, analysis is performed, knowledge levels unlock, and the skychart/encyclopedia updates. No real optical/sensor simulation yet — that comes in Sprint 09.

---

## Overview

Sprint 08 is the most conceptual sprint since Sprint 07. No telescopes, no CCDs,
no spectral rendering. Instead, it defines the systems that will make the entire
gameplay loop possible.

The deliverable is a **vertical slice** — not pretty, not realistic, but end-to-end.
At the end of this sprint, a mock "Magic Instrument" can:
1. Be pointed at a procedural star
2. Schedule an observation session of duration X
3. Accumulate SNR over time as the simulation clock advances
4. Produce a DataRecord when complete
5. The DataRecord is analyzed (automatically, since this is mock)
6. Knowledge levels unlock based on SNR thresholds
7. The object appears on the skychart (if previously undiscovered)
8. The info panel shows newly-discovered properties

This is the skeleton. Sprints 09+ will replace the "Magic Instrument" with real
telescopes, real spectrographs, real analysis tools.

---

## Architecture

### 1. Knowledge Levels (Property Stratification)

Every object type has a fixed number of knowledge levels. Each level gates
access to specific properties.

```cpp
namespace parallax::knowledge
{
    // Universal knowledge levels
    enum class KnowledgeLevel : u8
    {
        Unknown = 0,        // Object existence not known
        Detected = 1,       // Detected as a source (position, magnitude)
        Classified = 2,     // Basic type known (star type, galaxy morphology)
        Characterized = 3,  // Key properties measured (spectrum, distance)
        Detailed = 4,       // Advanced properties (rotation, composition)
        Resolved = 5,       // Structure resolved (sub-universe visible)
        FullyMapped = 6,    // Maximum characterization
        Reserved = 7        // Future use
    };

    // Historical knowledge: what the real catalogs already tell us.
    // This is the baseline for catalog objects at game start.
    //
    // Rule: real catalog objects start at L3 (Characterized).
    //       This means position, magnitude, color, basic classification are known.
    //       L4+ (sub-universe contents, detailed properties) must be discovered by the player.
}
```

**Example property mapping for a Star:**

| Level | Properties unlocked                                      |
|-------|----------------------------------------------------------|
| L1    | Position (RA/Dec), apparent magnitude                    |
| L2    | Color index (B-V), spectral class (rough)               |
| L3    | Precise spectrum, distance (parallax), radial velocity  |
| L4    | Rotation, magnetic field, detailed chemistry             |
| L5    | Sub-universe revealed (planetary system, companions)     |
| L6    | Activity cycles, granulation, starspot maps              |

**Example property mapping for a Galaxy:**

| Level | Properties unlocked                                      |
|-------|----------------------------------------------------------|
| L1    | Position, apparent magnitude, size                       |
| L2    | Morphology (Hubble type)                                 |
| L3    | Redshift, distance, integrated spectrum                  |
| L4    | Velocity dispersion, stellar populations                 |
| L5    | Sub-universe revealed (individual stars, nebulae)        |
| L6    | Dark matter profile, AGN details                         |

**Example property mapping for an Exoplanet:**

| Level | Properties unlocked                                      |
|-------|----------------------------------------------------------|
| L1    | Transit detected (existence)                             |
| L2    | Period, transit depth, transit duration                  |
| L3    | Radius, semi-major axis, mass (from RV), eccentricity    |
| L4    | Atmosphere composition (transit spectroscopy)            |
| L5    | Direct imaging, albedo, phase variation                  |
| L6    | Surface map, diurnal variations                          |

### 2. Observable Properties

Every property has metadata: which level unlocks it, what SNR is needed,
what measurement technique produces it.

```cpp
namespace parallax::knowledge
{
    enum class MeasurementTechnique : u16
    {
        None,
        BroadbandPhotometry,
        PrecisionPhotometry,    // for transits
        SpectroscopyLowRes,
        SpectroscopyHighRes,
        RadialVelocity,
        Astrometry,
        Interferometry,
        PolarimetryLinear,
        Coronagraphy,
        RadioObservation,
        XRayObservation,
        // ... sci-fi techniques added later
    };

    struct PropertyDescriptor
    {
        std::string name;                       // "orbital_period"
        KnowledgeLevel unlocks_at;              // L2
        MeasurementTechnique required_technique; // PrecisionPhotometry
        f32 required_snr;                       // 10.0 for confident detection
        f32 required_observation_hours;         // Hint for session duration
    };
}
```

### 3. Historical vs Discovered

At game start, the Player Knowledge Database is pre-populated with
historical knowledge for all real catalog objects.

```cpp
namespace parallax::knowledge
{
    struct ObjectKnowledge
    {
        u64 object_id;                  // Universe object ID
        KnowledgeLevel current_level;   // Highest level unlocked
        
        // Per-property measurements the player has made
        // (above and beyond historical baseline)
        std::unordered_map<std::string, MeasurementRecord> measurements;
        
        // Observation history
        std::vector<u64> observation_session_ids;
        
        // Detection confidence (for unconfirmed candidates)
        u32 independent_detections = 0;  // Count of separate sessions
        bool is_confirmed = false;       // 2+ independent detections
        
        bool is_historical = false;       // Was pre-populated at game start
    };

    class KnowledgeDatabase
    {
    public:
        // At game start, pre-populate with historical knowledge
        void initialize_from_historical_catalogs(const universe::Universe& universe);
        
        // Query: does the player know this object?
        [[nodiscard]] bool is_known(u64 object_id) const;
        
        // Query: what level of knowledge?
        [[nodiscard]] KnowledgeLevel get_level(u64 object_id) const;
        
        // Query: specific property known?
        [[nodiscard]] std::optional<MeasurementRecord> get_measurement(
            u64 object_id, const std::string& property) const;
        
        // Query: all known objects (for skychart rendering)
        [[nodiscard]] std::vector<u64> get_all_known_ids() const;
        
        // Mutation: add a new detection
        void add_detection(u64 object_id, u64 session_id);
        
        // Mutation: record a measurement
        void record_measurement(
            u64 object_id, 
            const std::string& property,
            f64 value, f32 uncertainty, f32 snr,
            u64 session_id);
        
        // Persistence
        void save(const std::filesystem::path& path) const;
        void load(const std::filesystem::path& path);
    
    private:
        std::unordered_map<u64, ObjectKnowledge> m_objects;
    };
}
```

**Historical initialization rule:**

For every object returned by `Universe::query_all_catalog_objects()` (the real
catalog contents only, not procedural), create an `ObjectKnowledge` entry with:
- `current_level = KnowledgeLevel::Characterized` (L3)
- `is_historical = true`
- `is_confirmed = true`
- Pre-populate measurements for properties that the real catalogs contain
  (position, magnitude, B-V, spectral type for stars; morphology for galaxies)

Procedural objects start without entries — they don't exist in the Knowledge DB
until the player discovers them.

### 4. Observation Sessions

```cpp
namespace parallax::observation
{
    enum class SessionType : u8
    {
        PointedObservation,  // Target a specific object
        SurveyScan           // Scan an area of sky for new sources
    };

    enum class SessionState : u8
    {
        Scheduled,
        InProgress,
        Completed,
        Aborted,
        Failed              // e.g., target set during session, weather, etc.
    };

    struct SessionParameters
    {
        SessionType type;
        u64 target_object_id = 0;           // For PointedObservation
        SkyRegion target_region;            // For SurveyScan
        u64 instrument_id;
        f64 planned_duration_hours;
        f64 start_julian_date;
        MeasurementTechnique technique;
    };

    struct SessionProgress
    {
        SessionState state;
        f64 elapsed_hours;
        f64 accumulated_snr;
        f32 completion_fraction;            // 0..1
        std::vector<std::string> log;       // Events during session
    };

    class ObservationSession
    {
    public:
        ObservationSession(u64 id, const SessionParameters& params);
        
        // Advance simulation by dt seconds
        // Updates SNR, handles interruptions (target sets, weather — future)
        void tick(f64 dt_seconds, 
                  const universe::Universe& universe,
                  const /* instrument */ void* instrument);
        
        [[nodiscard]] u64 get_id() const;
        [[nodiscard]] const SessionParameters& get_parameters() const;
        [[nodiscard]] const SessionProgress& get_progress() const;
        
        // Called when completed
        [[nodiscard]] std::unique_ptr<DataRecord> produce_data() const;
    
    private:
        u64 m_id;
        SessionParameters m_params;
        SessionProgress m_progress;
    };

    class SessionScheduler
    {
    public:
        // Schedule a new session
        u64 schedule(const SessionParameters& params);
        
        // Abort an in-progress session
        void abort(u64 session_id);
        
        // Update all active sessions with current time
        void update(f64 current_julian_date, f64 dt_seconds,
                   const universe::Universe& universe);
        
        // Query active and completed sessions
        [[nodiscard]] std::vector<const ObservationSession*> get_active() const;
        [[nodiscard]] std::vector<const ObservationSession*> get_completed() const;
        
        // Consume a completed session's data (moves it to archive)
        [[nodiscard]] std::unique_ptr<DataRecord> harvest(u64 session_id);
    
    private:
        std::unordered_map<u64, std::unique_ptr<ObservationSession>> m_sessions;
        u64 m_next_id = 1;
    };
}
```

### 5. Data Records

```cpp
namespace parallax::observation
{
    enum class DataType : u8
    {
        PhotometricMeasurement,  // Single flux measurement
        LightCurve,              // Time series of flux
        Spectrum,                // Wavelength vs flux
        Image,                   // 2D pixel data
        SurveySourceList,        // Catalog of detected sources
        Mock                     // Sprint 08 placeholder
    };

    struct DataRecord
    {
        u64 id;
        u64 session_id;
        u64 target_object_id;    // 0 if survey
        DataType type;
        MeasurementTechnique technique;
        f64 observation_jd;      // Time of observation
        f64 duration_hours;
        f32 achieved_snr;
        
        // Data payload — type-dependent
        // For Sprint 08, just store extracted measurements directly
        std::unordered_map<std::string, f64> measurements;
        std::unordered_map<std::string, f32> uncertainties;
        
        // Future: raw data blobs (image pixels, spectra, light curves)
        std::vector<u8> raw_data;
    };

    class DataArchive
    {
    public:
        void add(std::unique_ptr<DataRecord> record);
        
        [[nodiscard]] std::vector<const DataRecord*> get_by_target(u64 object_id) const;
        [[nodiscard]] std::vector<const DataRecord*> get_all() const;
        [[nodiscard]] const DataRecord* get_by_id(u64 record_id) const;
        
        void save(const std::filesystem::path& path) const;
        void load(const std::filesystem::path& path);
    
    private:
        std::unordered_map<u64, std::unique_ptr<DataRecord>> m_records;
    };
}
```

### 6. Sub-Universe Hierarchy

Every `CelestialObject` can optionally be a container with its own sub-universe.

```cpp
// Extension to CelestialObject from Sprint 07:
struct CelestialObject
{
    // ... existing fields ...
    
    // Sub-universe support
    bool is_container = false;
    u64 sub_universe_seed = 0;
    f32 containment_angular_radius_arcsec = 0.0f;  // For resolution check
    u64 parent_container_id = 0;  // 0 = Milky Way top-level
};
```

**Rule for entering a sub-universe:**

A sub-universe becomes "enterable" when the observing instrument achieves
angular resolution better than `containment_angular_radius_arcsec`.

- M31 has containment radius ~3 arcmin (galaxy size). Any instrument can "see" M31.
  But resolving its individual stars requires arcsecond-level resolution.
- Sirius has containment radius ~7.5 arcsec (orbit of Sirius B).
  Resolving Sirius B requires high-res imaging or interferometry.
- A G-type star at 10 pc has containment radius ~0.5 arcsec (habitable zone).
  Direct imaging of planets requires coronagraphy.

The Knowledge System tracks whether the player has "resolved into" a sub-universe
(level L5 = Resolved). Once resolved, objects inside become queryable.

**In Sprint 08**, we only lay the architecture. Sub-universe content generation
is done by `ProceduralProvider::generate_sub_universe(parent_id, seed)` but
actually generating stars inside M31 is left for later sprints.

### 7. The Vertical Slice

For Sprint 08 to be considered complete, there must be ONE working end-to-end flow:

```
1. A procedural star exists in Universe (generated from seed)
   - It's NOT in the Knowledge DB (not discovered yet)
   - Not visible on the skychart

2. Player opens the "Mock Instrument" panel (new UI)
   - Selects the procedural star by its procedural ID (or by coordinates)
   - Sets observation duration: e.g., 4 hours
   - Clicks "Schedule Observation"

3. Session enters Scheduled state
   - Visible in the Sessions Panel (new UI)

4. Time advances (either player clicks "wait" or accelerates clock)
   - Session transitions to InProgress at start_julian_date
   - Each tick, SNR accumulates based on mock formula:
       snr_per_hour = 5.0  (fixed for mock instrument)
       accumulated_snr += snr_per_hour * dt_hours
   - Progress shown in UI

5. When elapsed_hours >= planned_duration_hours, session completes
   - A DataRecord is produced
   - Added to DataArchive
   - UI shows "Observation Complete" notification

6. Analysis runs automatically (mock analysis)
   - For SNR >= 10: detection at L1, add to Knowledge DB
   - For SNR >= 30: classification at L2, add spectral type
   - For SNR >= 100: characterization at L3, add distance

7. Skychart refreshes
   - The procedural star is now visible (purple/new color to distinguish)
   - Info panel shows the newly unlocked properties
   - Label indicates "1 detection" or "Confirmed" depending on count

8. Player can repeat the observation to confirm (2nd independent detection)
   - After 2 detections, is_confirmed = true
   - Visual style changes from "candidate" to "confirmed"
```

This flow uses:
- MockInstrument (placeholder for real instruments in Sprint 09+)
- Mock analysis (placeholder for real analysis tools)
- Simplified UI panels (will be refined later)

But it exercises every subsystem: session scheduling, time progression,
SNR accumulation, data production, knowledge update, skychart rendering,
confirmation logic.

---

## Tasks

### Task 8.1 — Knowledge: Core Types

Create the foundational types for knowledge, properties, measurements.

**Files:**
- `src/knowledge/knowledge_level.hpp`
- `src/knowledge/measurement_technique.hpp`
- `src/knowledge/property_descriptor.hpp`
- `src/knowledge/measurement_record.hpp`

**Details:**
- KnowledgeLevel enum (L0-L7)
- MeasurementTechnique enum (all current + placeholder sci-fi)
- PropertyDescriptor struct with metadata
- MeasurementRecord struct with value, uncertainty, SNR, session ID

Header-only where possible.
Follow CLAUDE.md conventions. Update CMakeLists.

**Acceptance:** Types compile and are usable by subsequent tasks.

---

### Task 8.2 — Knowledge: Property Registry

Static registry of all observable properties and their metadata.

**Files:**
- `src/knowledge/property_registry.hpp`
- `src/knowledge/property_registry.cpp`

**Details:**

A static database listing all possible properties for each object type.
This is the "what is there to discover" catalog.

```cpp
namespace parallax::knowledge
{
    class PropertyRegistry
    {
    public:
        // Get all properties for an object type
        [[nodiscard]] static std::span<const PropertyDescriptor> 
            get_properties(universe::ObjectType type);
        
        // Get property by name
        [[nodiscard]] static std::optional<PropertyDescriptor> 
            get_property(universe::ObjectType type, std::string_view name);
        
        // Get properties unlocked at a specific level
        [[nodiscard]] static std::vector<PropertyDescriptor>
            get_properties_for_level(universe::ObjectType type, KnowledgeLevel level);
    };
}
```

Initialize with properties per the tables in the architecture section:
- Stars: position, mag_v, color_bv, spectral_type, distance_pc, rv_kms, rotation_kms, b_field_g, ...
- Galaxies: position, mag_v, size_arcmin, hubble_type, redshift, velocity_dispersion, ...
- Exoplanets: period_days, transit_depth, radius_earth, mass_earth, atmosphere, ...
- DSOs: position, mag_v, size_arcmin, subtype, expansion_velocity, ...

At least 5-10 properties per object type with realistic SNR and time estimates.

**Acceptance:** Registry returns correct property lists. Test with each object type.

---

### Task 8.3 — Knowledge: Database

The per-player knowledge database.

**Files:**
- `src/knowledge/object_knowledge.hpp`
- `src/knowledge/knowledge_database.hpp`
- `src/knowledge/knowledge_database.cpp`

**Details:**

Implement the `KnowledgeDatabase` class described in the architecture.

- `ObjectKnowledge` struct tracks level, measurements, detection count
- `initialize_from_historical_catalogs()` pre-populates entries for all
  real catalog objects (Hipparcos, Tycho-2, Messier, solar system) at L3
- Population of historical measurements: pull data from Universe providers
  (RA, Dec, mag_v, B-V are in the CelestialObject already)
- `is_known()`, `get_level()`, `get_measurement()` query methods
- `get_all_known_ids()` returns list for skychart rendering
- `add_detection()` increments count, promotes to confirmed at 2+
- `record_measurement()` stores new measurement, updates level if needed
- Persistence: JSON or binary serialization (pick whichever is easier)

**Acceptance:**
- At game start, all Hipparcos stars are in the DB at L3 with their properties
- Save and load roundtrip works
- Querying unknown procedural object returns is_known() = false

---

### Task 8.4 — Observation: Core Types

Create the session and data record types.

**Files:**
- `src/observation/session_types.hpp`
- `src/observation/data_record.hpp`

**Details:**
- SessionType, SessionState enums
- SessionParameters, SessionProgress structs
- DataType enum
- DataRecord struct with measurements map

Header-only where possible.
Follow CLAUDE.md conventions.

---

### Task 8.5 — Observation: Session Scheduler

Scheduling and lifecycle management of observation sessions.

**Files:**
- `src/observation/observation_session.hpp`
- `src/observation/observation_session.cpp`
- `src/observation/session_scheduler.hpp`
- `src/observation/session_scheduler.cpp`

**Details:**

Implement `ObservationSession` and `SessionScheduler` from the architecture.

Critical: the `tick()` method must handle:
- Transition from Scheduled → InProgress when current_jd >= start_jd
- Accumulate SNR based on mock formula (Sprint 08: flat rate per hour)
- Transition from InProgress → Completed when elapsed >= planned
- Optional: detect target below horizon, set state to Failed (Sprint 09+)

The mock SNR formula for Sprint 08:
```cpp
f64 snr_rate_per_hour = 5.0;  // For mock instrument
f64 snr_gain = snr_rate_per_hour * dt_hours;
m_progress.accumulated_snr += snr_gain;

// SNR in quadrature (but simplified to linear accumulation for mock)
```

Scheduler receives the current simulation time from Application
(already present — we drive it from the existing time system).

When the Application advances simulation time, it calls
`SessionScheduler::update(jd, dt)` once per frame with the new time.

**Acceptance:**
- Can schedule a session with valid parameters
- Session progresses over simulated time
- Session completes when duration elapsed
- Produces a DataRecord on completion
- Abort works

---

### Task 8.6 — Observation: Data Archive

Persistent storage for all data records.

**Files:**
- `src/observation/data_archive.hpp`
- `src/observation/data_archive.cpp`

**Details:**
- Implements `DataArchive` from the architecture
- Store `unique_ptr<DataRecord>` in a map by ID
- Query by target object, by session, by ID
- Serialize to file (JSON is fine for Sprint 08 — binary later)

**Acceptance:** Can add, retrieve, list, and persist data records.

---

### Task 8.7 — Mock Instrument and Analysis

A placeholder instrument and automatic analysis to enable end-to-end testing.

**Files:**
- `src/instruments/mock_instrument.hpp`
- `src/instruments/mock_instrument.cpp`
- `src/analysis/mock_analyzer.hpp`
- `src/analysis/mock_analyzer.cpp`

**Details:**

`MockInstrument`:
- Has an ID, name "Magic Instrument"
- Single capability: can observe anything with any technique
- SNR formula: flat 5.0 per hour regardless of target brightness or technique
- This is NOT a real instrument — it's the placeholder that Sprint 09+ replaces.

`MockAnalyzer`:
- Takes a DataRecord and an object ID from Universe
- Looks up the object's procedural properties
- Based on achieved SNR, writes measurements to the DataRecord:
  - SNR >= 10:   L1 unlocked — position, mag_v, mag_b exposed
  - SNR >= 30:   L2 unlocked — spectral type, classification exposed
  - SNR >= 100:  L3 unlocked — distance, detailed spectrum exposed
- Returns a list of updates to apply to the Knowledge Database

This is the "magic" that will be replaced by real analysis tools later,
where the player manually extracts properties from light curves, spectra, etc.

**Acceptance:**
- MockInstrument integrates with SessionScheduler
- Completed sessions get analyzed by MockAnalyzer
- Analysis produces correct knowledge updates based on SNR

---

### Task 8.8 — Universe Extension: Sub-Universe Foundation

Add sub-universe fields to CelestialObject and supporting Universe methods.

**Files:** Updates to `src/universe/celestial_object.hpp` and Universe class.

**Details:**

Extend `CelestialObject` with:
```cpp
bool is_container = false;
u64 sub_universe_seed = 0;
f32 containment_angular_radius_arcsec = 0.0f;
u64 parent_container_id = 0;
```

For objects that ARE containers, populate the fields. Examples:
- M31: is_container=true, seed=hash("M31"), radius=180arcmin (apparent size)
- Sirius: is_container=true, seed=hash("HIP 32349"), radius=7.5arcsec
- Any G-type star in catalog: is_container=true, seed=hash("HIP XXXXX"), radius=0.5arcsec (HZ)

Add to `Universe`:
```cpp
// Check if sub-universe is accessible with given angular resolution
[[nodiscard]] bool can_resolve_sub_universe(
    u64 object_id, f32 instrument_angular_resolution_arcsec) const;

// Get parent object for a sub-universe object (ID decoding)
[[nodiscard]] std::optional<u64> get_parent(u64 object_id) const;
```

Do NOT implement actual sub-universe content generation in this sprint.
`ProceduralProvider::generate_sub_universe()` stub exists but returns empty list.
The important thing is that the ARCHITECTURE is ready for Sprint 09+.

**Acceptance:** CelestialObjects have sub-universe fields. Universe methods work.

---

### Task 8.9 — Skychart Refactor: Historical + Discovered Rendering

Update the skychart to respect Knowledge Database filtering.

**Files:** Updates to `src/core/application.cpp` frame loop, star/dso/solar system renderers.

**Details:**

**New rendering rule:**
- Historical catalog objects (`is_historical=true` in Knowledge DB): always rendered with L1-L3 data
- Procedural objects: rendered ONLY if in Knowledge DB (i.e., discovered by player)
- Confirmed objects: normal rendering
- Unconfirmed candidates: rendered with distinctive visual style (dimmer, or pulsing, or different color)

**Frame loop change:**

```cpp
// OLD (Sprint 07):
auto objects = m_universe->query_fov(ra, dec, radius, mag_limit);

// NEW (Sprint 08):
auto all_objects = m_universe->query_fov(ra, dec, radius, mag_limit);
std::vector<RenderableObject> to_render;

for (const auto& obj : all_objects)
{
    auto knowledge = m_knowledge_db->get(obj.id);
    
    if (obj.is_real_catalog()) {
        // Historical: always render with full historical data
        to_render.push_back({obj, RenderStyle::Historical, L3});
    } else {
        // Procedural: only if known
        if (knowledge.has_value()) {
            auto style = knowledge->is_confirmed 
                ? RenderStyle::Confirmed 
                : RenderStyle::Candidate;
            to_render.push_back({obj, style, knowledge->current_level});
        }
        // else: not visible
    }
}
```

**Info panel update:**
The info panel for a selected object now shows:
- Historical object: all L1-L3 properties as before
- Discovered procedural at L1: position, magnitude only
- At L2: + spectral/classification info
- At L3: + distance, detailed properties
- L4+ shown only if the player has explicit measurements for those properties

**Magnitude limit reinterpretation:**
- For historical objects: current behavior (filters by apparent magnitude)
- For procedural objects: filters by apparent magnitude among DISCOVERED ones
- Undiscovered procedural stars are never rendered regardless of mag limit

**Acceptance:**
- At game start, all historical catalog objects visible (same as Sprint 07 with mag_limit)
- No procedural stars visible even at MLIM 18 (nothing discovered yet)
- After mock observation → confirmed detection → procedural star appears
- Candidate style visually distinct from confirmed style

---

### Task 8.10 — UI: Sessions Panel, Instrument Panel, Data Archive Panel

New UI panels for the observation workflow.

**Files:**
- `src/ui/instrument_panel.hpp/cpp` — choose instrument, target, duration, schedule
- `src/ui/sessions_panel.hpp/cpp` — list active and completed sessions
- `src/ui/data_archive_panel.hpp/cpp` — browse collected data records
- `src/ui/info_panel.cpp` — update to show knowledge-filtered properties

**Details:**

**Instrument Panel** (accessible from toolbar, new button "OBSERVE"):
```
┌─── OBSERVE ───────────────┐
│                            │
│ Instrument:                │
│ [Magic Instrument (mock)] ▼│
│                            │
│ Target:                    │
│  ● Selected Object         │
│    (HIP 12345)             │
│  ○ Survey Scan             │
│    Center: ........        │
│                            │
│ Technique:                 │
│ [Photometry] ▼             │
│                            │
│ Duration:                  │
│ [━━━●━━━] 4.0 hours       │
│                            │
│  [SCHEDULE]  [CANCEL]      │
│                            │
└────────────────────────────┘
```

**Sessions Panel** (right side, new button "SESSIONS"):
```
┌─── OBSERVATION SESSIONS ──┐
│                            │
│ ACTIVE                     │
│ ─────────                  │
│ ★ HIP 12345                │
│   Mock / Photometry        │
│   [████░░░░] 47% (1.9h)   │
│   SNR: 9.5 → target 20.0  │
│   [ABORT]                  │
│                            │
│ COMPLETED (3)              │
│ ─────────                  │
│ ✓ HIP 12345  (L2 reached)  │
│ ✓ M31 area   (survey)      │
│ ✓ HIP 54321  (L1 reached)  │
│                            │
└────────────────────────────┘
```

**Data Archive Panel** (bottom, new button "DATA"):
Simple list for Sprint 08. Full data visualization in later sprints.
```
┌─── DATA ARCHIVE ──────────┐
│ JD          TARGET   SNR  │
│ 2461104.5   HIP 12345 35  │
│ 2461103.2   HIP 54321 12  │
│ 2461102.0   Survey    --  │
└────────────────────────────┘
```

**Info Panel updates:**
Show knowledge level indicator. Show "?" for unmeasured properties.
For candidates, show "UNCONFIRMED" badge.

**Acceptance:** All panels functional. End-to-end flow works through UI.

---

### Task 8.11 — Integration & Persistence

Tie everything together in Application and add save/load.

**Changes to:** `src/core/application.hpp/cpp`

**Details:**

Add to Application:
```cpp
std::unique_ptr<knowledge::KnowledgeDatabase> m_knowledge;
std::unique_ptr<observation::SessionScheduler> m_scheduler;
std::unique_ptr<observation::DataArchive> m_archive;
std::unique_ptr<instruments::MockInstrument> m_mock_instrument;
std::unique_ptr<analysis::MockAnalyzer> m_analyzer;
```

In `init()`:
1. Create Universe (already there)
2. Create KnowledgeDatabase, initialize from historical catalogs
3. Create Scheduler, Archive, MockInstrument, MockAnalyzer
4. Load player save if exists

In frame loop, after time update:
```cpp
m_scheduler->update(m_julian_date, dt_seconds, *m_universe);

// Harvest any newly completed sessions
for (auto* session : m_scheduler->get_completed())
{
    auto data = m_scheduler->harvest(session->get_id());
    m_archive->add(data.get());  // Keep ptr for analyzer
    
    auto updates = m_analyzer->analyze(*data, *m_universe);
    for (const auto& u : updates)
        m_knowledge->apply(u);
    
    // Show notification in UI
}
```

In `shutdown()`:
- Save knowledge DB and archive to user data dir

**Save files location:**
- Windows: `%APPDATA%/Parallax/save/`
- Linux: `$XDG_DATA_HOME/parallax/save/` or `~/.local/share/parallax/save/`

Files:
- `knowledge.json` (or .bin)
- `archive.json`
- `sessions.json` (in-progress sessions to resume)

**Acceptance (Sprint 08 Definition of Done):**
- [ ] KnowledgeDatabase initialized with all historical catalog objects at L3
- [ ] Procedural objects are NOT in the initial skychart
- [ ] Player can open Instrument Panel and schedule a mock observation
- [ ] Target = selected procedural object (via coordinate input or random picker)
- [ ] Session appears in Sessions Panel with progress bar
- [ ] Time acceleration advances the session; SNR accumulates
- [ ] Session completion produces a DataRecord in the Archive
- [ ] MockAnalyzer processes the record and updates KnowledgeDatabase
- [ ] After first detection: procedural star appears on skychart as "candidate"
- [ ] After second observation: star promoted to "confirmed"
- [ ] Info panel shows only properties at or below the player's knowledge level
- [ ] Save/load works: close program, reopen, knowledge preserved
- [ ] Historical objects still render correctly (no regressions)
- [ ] All keyboard shortcuts still work
- [ ] No Vulkan validation errors
- [ ] ≥ 60fps

---

## Task Order

```
8.1 → 8.2 → 8.3 → 8.4 → 8.5 → 8.6 → 8.7 → 8.8 → 8.9 → 8.10 → 8.11
 │     │     │     │     │     │     │     │     │     │      │
 types registry DB  types scheduler archive mock subuni sky    ui    integrate
                                          instr        refactor
```

The critical path: types → registry → knowledge DB → session system → mock instrument → analyzer → integration. The UI and skychart refactor come last because they consume all the previous systems.

---

## Deliberate Simplifications (for Sprint 08)

These shortcuts keep the sprint tractable. All will be revisited in later sprints.

- **No realistic SNR formula**: flat 5.0/hour regardless of target, instrument, or technique. Sprint 09+ will use aperture, exposure time, target magnitude, sky brightness, seeing, etc.
- **No manual analysis**: MockAnalyzer auto-derives properties. Sprint 10+ will add light curve viewers, periodogram tools, spectral fitters, etc.
- **No sub-universe content generation**: architecture exists, but resolving into M31 or a stellar system produces an empty list.
- **No weather, horizon constraints, target availability checks**: sessions run regardless. Sprint 11+ will add realistic scheduling.
- **No telescope queue / conflict management**: one session at a time is fine.
- **Data records are just property maps**: no raw pixel data, no spectrum arrays. Those come with real instruments.
- **Save format**: JSON is fine. Binary serialization later.
- **UI is functional, not polished**: panels look basic. Polish in Sprint 11+.

---

## Architecture Diagram (Updated)

```
┌────────────────────────────────────────────────────────────────────┐
│                            UNIVERSE                                 │
│           (Sprint 07 — ground truth, never changes)                 │
│   Real catalogs + Procedural generation + Sub-universe seeds        │
└──────────────────┬─────────────────────────────────────────────────┘
                   │
         ┌─────────┴─────────┐
         │                   │
         ▼                   ▼
┌─────────────────┐  ┌──────────────────────────────────────────────┐
│  HISTORICAL     │  │  PLAYER KNOWLEDGE DATABASE (Sprint 08)        │
│  KNOWLEDGE      │  │   Per-player discoveries beyond historical    │
│  (auto-populated│  │   - Procedural discoveries                    │
│   at game start │  │   - L4+ measurements of historical objects    │
│   for all real  │  │   - Observation history                       │
│   catalog       │  │   - Detection counts, confirmation status     │
│   objects at L3)│  │                                               │
└─────────────────┘  └──────────────────────────────────────────────┘
                   │                   ▲
                   │                   │ updates via
                   ▼                   │ MockAnalyzer (Sprint 08)
                                       │ → real analysis (Sprint 10+)
          ┌────────────────┐           │
          │    SKYCHART    │           │
          │   (Sprint 08   │           │
          │    refactor)   │           │
          │ Shows:         │           │
          │  - Historical  │           │
          │  - Discovered  │           │
          │    procedural  │           │
          └────────────────┘           │
                                       │
          ┌────────────────┐           │
          │  OBSERVATION   │           │
          │  SUBSYSTEM     │───────────┘ produces DataRecords
          │  (Sprint 08)   │
          │  - Scheduler   │
          │  - Sessions    │
          │  - Archive     │
          └───────┬────────┘
                  │
                  │ uses
                  ▼
          ┌────────────────┐
          │  INSTRUMENTS   │
          │  (Sprint 08:   │
          │   MockInstr)   │
          │  (Sprint 09+:  │
          │   real instrs) │
          └────────────────┘
```
