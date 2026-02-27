# Catalog System

## Overview

The catalog system manages astronomical data from real catalogs (Hipparcos, Tycho-2, Gaia DR3)
and provides fast spatial queries for the rendering pipeline.

Design priorities:
1. **Fast FOV queries** — return all stars in a sky region within 1ms
2. **Magnitude filtering** — only load stars brighter than current limit
3. **Streaming** — full Gaia DR3 (~57 GB) cannot fit in RAM, must stream
4. **Memory-mapped I/O** — let the OS handle paging

---

## Binary Catalog Format (.plxcat)

### File Structure

```
┌──────────────────────────────────────┐
│ Header (64 bytes)                    │
├──────────────────────────────────────┤
│ HEALPix Index Table                 │
│ (pixel_id → offset, count) × N      │
├──────────────────────────────────────┤
│ Star Data (sorted by HEALPix pixel,  │
│ within each pixel sorted by mag)     │
└──────────────────────────────────────┘
```

### Header (64 bytes)

```cpp
struct CatalogHeader
{
    char magic[8];          // "PLX_CAT\0"
    uint32_t version;       // Format version (1)
    uint32_t flags;         // Reserved
    uint64_t entry_count;   // Total star count
    uint32_t entry_size;    // Bytes per entry (32)
    uint32_t healpix_nside; // HEALPix resolution (e.g., 64 → 49152 pixels)
    uint32_t healpix_count; // 12 * nside * nside
    uint32_t reserved_0;
    uint64_t index_offset;  // Byte offset to index table
    uint64_t data_offset;   // Byte offset to star data
    uint8_t  padding[8];    // Pad to 64 bytes
};
```

### Star Entry (32 bytes, packed)

```cpp
#pragma pack(push, 1)
struct StarEntry
{
    double ra;              // Right ascension (radians) [8 bytes]
    double dec;             // Declination (radians) [8 bytes]
    uint16_t mag_v;         // V magnitude × 1000 (e.g., 4560 = 4.560) [2 bytes]
    uint16_t mag_b;         // B magnitude × 1000 [2 bytes]
    uint16_t parallax;      // Parallax in 0.01 mas units [2 bytes]
    uint8_t spectral_type;  // Encoded: O=0..M=6, subtype in bits [1 byte]
    uint8_t flags;          // Bit flags: variable, binary, etc. [1 byte]
    uint32_t source_id;     // Cross-reference ID [4 bytes]
    uint32_t reserved;      // Future use [4 bytes]
};                          // Total: 32 bytes
#pragma pack(pop)
```

Magnitude encoding: `int16_t` with 3 decimal places (range: -1.460 to 32.767).
Sufficient for all practical astronomical magnitudes.

### HEALPix Index Entry (16 bytes)

```cpp
struct HEALPixIndexEntry
{
    uint64_t offset;        // Byte offset into data section
    uint32_t count;         // Number of stars in this pixel
    uint32_t reserved;
};
```

---

## HEALPix Spatial Indexing

HEALPix (Hierarchical Equal Area isoLatitude Pixelization) divides the sky sphere
into equal-area pixels. Used by Gaia, Planck, and all major sky surveys.

### Why HEALPix over Octree

| Criteria          | HEALPix             | Octree                  |
|-------------------|----------------------|-------------------------|
| Sky-sphere native | Yes                  | No (3D, needs adaptation)|
| Equal area        | Yes                  | No                      |
| Standard in astro | Yes (Gaia, Planck)   | No                      |
| FOV query         | O(pixels in FOV)     | O(log n) tree traversal |
| Disk-friendly     | Yes (sorted, contiguous)| No (pointer-heavy)   |
| Memory-map safe   | Yes                  | Difficult               |

### Resolution Choice

```
nside = 64  →  49,152 pixels  → ~0.84 deg² per pixel
nside = 128 →  196,608 pixels → ~0.21 deg² per pixel
nside = 256 →  786,432 pixels → ~0.05 deg² per pixel
```

For Phase 1: **nside = 64** (sufficient for naked-eye to small telescope FOV).
For Gaia full catalog: **nside = 256** (needed for narrow FOV deep queries).

### FOV Query Algorithm

```
Input: center (RA, Dec), radius (degrees)
1. Convert center to HEALPix pixel
2. query_disc(center, radius) → list of overlapping pixel IDs
3. For each pixel ID:
   a. Look up index entry → (offset, count)
   b. Memory-map or read star data at offset
   c. Filter by magnitude limit
   d. Filter by exact angular distance (HEALPix is approximate)
4. Return filtered star list
```

---

## Catalog Build Pipeline (Offline Tool)

```
Raw Gaia DR3 CSV files (550+ GB compressed)
  → Parse relevant columns (ra, dec, phot_g_mean_mag, bp_rp, parallax)
  → Convert magnitudes: Gaia G → Johnson V (approximate transform)
  → Assign HEALPix pixel (nested scheme)
  → Sort by (healpix_pixel, magnitude)
  → Write binary .plxcat file
  → Build index table
```

Tool: `tools/catalog_converter/` — standalone C++ program.
Can process in streaming fashion (no full dataset in RAM).

### Catalog Tiers

| File              | Source        | Stars      | Size     | Phase |
|-------------------|---------------|------------|----------|-------|
| `bright.plxcat`   | Hipparcos     | 118,218    | ~3.6 MB  | 1     |
| `tycho2.plxcat`   | Tycho-2       | 2,539,913  | ~77 MB   | 1     |
| `gaia_dr3.plxcat` | Gaia DR3      | 1,811,709,771 | ~55 GB | 1-2  |

Phase 1 starts with `bright.plxcat` and `tycho2.plxcat`.
Gaia streaming added in Phase 1 (late) or Phase 2.

---

## Runtime Architecture

```cpp
namespace parallax::catalog
{
    class CatalogManager
    {
    public:
        /// Load a .plxcat file (memory-mapped)
        void load(const std::filesystem::path& path);

        /// Query stars visible in a sky region
        /// Returns stars sorted by magnitude (brightest first)
        [[nodiscard]] std::vector<StarEntry> query_fov(
            double ra_center,       // radians
            double dec_center,      // radians
            double radius,          // radians
            float mag_limit         // faintest magnitude to include
        ) const;

        /// Get total loaded star count
        [[nodiscard]] uint64_t get_star_count() const;

    private:
        struct LoadedCatalog
        {
            std::unique_ptr<MemoryMappedFile> file;
            CatalogHeader header;
            std::span<const HEALPixIndexEntry> index;
            std::span<const StarEntry> data;
        };

        std::vector<LoadedCatalog> m_catalogs;
    };
}
```

### Memory-Mapped File Access

```cpp
class MemoryMappedFile
{
public:
    explicit MemoryMappedFile(const std::filesystem::path& path);
    ~MemoryMappedFile();

    [[nodiscard]] const uint8_t* data() const;
    [[nodiscard]] size_t size() const;

private:
#ifdef _WIN32
    HANDLE m_file = INVALID_HANDLE_VALUE;
    HANDLE m_mapping = nullptr;
#else
    int m_fd = -1;
#endif
    void* m_data = nullptr;
    size_t m_size = 0;
};
```

Platform-specific: `CreateFileMapping`/`MapViewOfFile` on Windows,
`mmap` on Linux.

---

## Performance Budget

Target: **< 2ms** for full FOV query + filter at 5° FOV, mag limit 14.

Breakdown:
- HEALPix pixel lookup: ~0.01ms
- Index lookup (memory-mapped): ~0.01ms
- Data scan + magnitude filter: ~1-2ms (depends on density)
- Copy to output vector: ~0.1ms
