#pragma once

/// @file procedural_provider.hpp
/// @brief ProceduralProvider — generates deterministic procedural stars beyond
///        the Tycho-2 magnitude limit (mag > 12).
///
/// Implements the DataProvider interface.  The sky is partitioned into HEALPix
/// nested cells (nside = 64, 49152 cells ≈ 0.84 deg² each).  Each cell is
/// generated on demand and cached; star positions, magnitudes, and colours are
/// fully deterministic given the master seed and the cell pixel ID.
///
/// This is the foundation for all future procedural content in Parallax.

#include "universe/data_provider.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace parallax::universe
{

/// @brief DataProvider that generates procedural stars beyond mag 12.
///
/// Ownership model:
///   @c m_cell_cache  stores per-cell vectors of pre-converted CelestialObjects,
///   protected by @c m_cache_mutex.  An LRU list (@c m_lru_order) tracks access
///   order for eviction when the cache exceeds @c kMaxCachedCells.
///
/// ID encoding:
///   @c encode_id(ObjectType::ProceduralStar, source_id)
///   where source_id = lower 56 bits of a hash of (pixel_id, star_index).
///
///   Reverse lookup via @c query_object is NOT implemented for procedural IDs in
///   this task — the method returns @c std::nullopt with a TODO comment.
///
/// Thread safety:
///   @c query_fov is safe to call from the render thread.  Cache mutations are
///   protected by @c m_cache_mutex.  All other methods are single-threaded.
class ProceduralProvider final : public DataProvider
{
public:
    ProceduralProvider()                                             = default;
    ~ProceduralProvider() override                                   = default;

    ProceduralProvider(const ProceduralProvider&)                   = delete;
    ProceduralProvider& operator=(const ProceduralProvider&)        = delete;

    ProceduralProvider(ProceduralProvider&&)                        = default;
    ProceduralProvider& operator=(ProceduralProvider&&)             = default;

    // -------------------------------------------------------------------------
    // Seed management
    // -------------------------------------------------------------------------

    /// @brief Set the master seed that controls all procedural generation.
    ///
    /// Clears the cell cache — all previously generated cells will be
    /// regenerated lazily on the next query.
    void set_master_seed(std::uint64_t seed);

    /// @brief Return the current master seed.
    [[nodiscard]] std::uint64_t master_seed() const noexcept { return m_master_seed; }

    // -------------------------------------------------------------------------
    // DataProvider overrides
    // -------------------------------------------------------------------------

    /// @brief Append procedural stars inside the query cone to @p results.
    ///
    /// Early-outs:
    ///   - @p flags does not include QueryFlags::Procedural.
    ///   - @p mag_limit ≤ kMinProceduralMag (12.0) — no procedural stars visible.
    ///
    /// @param ra         Cone centre RA (radians, J2000).
    /// @param dec        Cone centre Dec (radians, J2000).
    /// @param radius_deg Half-angle of the query cone (degrees).
    /// @param mag_limit  Faintest magnitude to include (inclusive).
    /// @param flags      Bitmask of object categories.
    /// @param results    Output vector — objects are appended, never cleared.
    void query_fov(double     ra,
                   double     dec,
                   double     radius_deg,
                   float      mag_limit,
                   QueryFlags flags,
                   std::vector<CelestialObject>& results) const override;

    /// @brief Look up a single procedural star by its packed u64 id.
    ///
    /// @note Reverse lookup of procedural star IDs is not implemented in this task.
    ///       Always returns @c std::nullopt for ProceduralStar IDs.
    ///       TODO: implement a reverse-lookup map (hash → CelestialObject) in a future
    ///             task when selection/detail panels need procedural-star info.
    ///
    /// Returns @c std::nullopt for any non-ProceduralStar ID immediately.
    [[nodiscard]] std::optional<CelestialObject> query_object(u64 id) const override;

    /// @brief Estimated count of procedural stars in the currently cached cells.
    ///
    /// Returns the sum of star counts across all cached cells.  This is NOT the
    /// total possible procedural star count (which is effectively unbounded) —
    /// only a "warm cache" estimate.  Returns 0 when the cache is empty.
    [[nodiscard]] std::size_t get_count() const override;

private:
    // -------------------------------------------------------------------------
    // Generation constants
    // -------------------------------------------------------------------------

    /// @brief HEALPix resolution (nside = 64 → 49152 cells ≈ 0.84 deg² each).
    static constexpr std::int64_t kNside = 64;

    /// @brief Maximum number of cached cells before LRU eviction triggers.
    static constexpr std::size_t kMaxCachedCells = 4096;

    /// @brief Procedural stars start at mag 12 — Tycho-2 covers brighter objects.
    static constexpr float kMinProceduralMag = 12.0f;

    /// @brief Maximum magnitude generated per cell (independent of query mag_limit).
    /// Generation goes to 16 to cover future queries without cache invalidation.
    static constexpr float kMaxGenerationMag = 16.0f;

    // -------------------------------------------------------------------------
    // Cache types
    // -------------------------------------------------------------------------

    /// @brief Cached data for one HEALPix cell.
    ///
    /// Stores pre-converted CelestialObjects so query_fov can filter and return
    /// them directly without per-query conversion overhead.  Also carries the
    /// iterator into m_lru_order for O(1) LRU update.
    ///
    /// star_uvecs holds unit vectors (x, y, z) for each star's (ra, dec) position,
    /// precomputed at generation time to avoid trig in the hot query_fov filtering loop.
    struct CellData
    {
        std::vector<CelestialObject>           stars;
        std::vector<std::array<float, 3>>      star_uvecs; ///< unit vectors per star
        std::list<std::int64_t>::iterator      lru_it;     ///< Iterator in m_lru_order.
    };

    // -------------------------------------------------------------------------
    // Private state
    // -------------------------------------------------------------------------

    /// Master seed controlling all procedural generation.
    std::uint64_t m_master_seed{0};

    /// HEALPix nside parameter (stored for future tunability).
    std::int64_t m_nside{kNside};

    /// Per-cell cache: pixel_id → CellData.
    mutable std::unordered_map<std::int64_t, CellData> m_cell_cache;

    /// LRU access order: front = most recently used, back = least recently used.
    mutable std::list<std::int64_t> m_lru_order;

    /// Guards all cache mutations.  query_fov is const but must update the cache.
    mutable std::mutex m_cache_mutex;

    /// Precomputed unit vectors for all HEALPix pixel centres.
    /// Computed once on first query_fov call — avoids O(nside²) pix2ang_nest calls
    /// per query.  Stored as float for cache-efficiency (adequate for rendering).
    mutable std::vector<std::array<float, 3>> m_pixel_uvecs;

    /// Logging / stats (mutable because updated in const query_fov).
    mutable bool          m_first_query_logged{false};
    mutable std::uint64_t m_query_count{0};
    mutable std::uint64_t m_cache_hits{0};

    // -------------------------------------------------------------------------
    // Private helpers (implemented in procedural_provider.cpp)
    // -------------------------------------------------------------------------

    /// @brief Ensure cell @p pixel_id is in the cache; generate it if missing (thread-safe).
    void ensure_cell_generated(std::int64_t pixel_id) const;

    /// @brief Generate and return the star list for cell @p pixel_id deterministically.
    ///
    /// Does NOT lock m_cache_mutex — callers must do so if needed.
    [[nodiscard]] CellData generate_cell(std::int64_t pixel_id) const;

    /// @brief Evict the least-recently-used cell if cache size exceeds kMaxCachedCells.
    ///
    /// Caller must hold m_cache_mutex.
    void evict_lru_if_needed() const;
};

} // namespace parallax::universe
