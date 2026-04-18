#pragma once

/// @file data_provider.hpp
/// @brief Abstract base class for all Universe Engine data providers.

#include "universe/celestial_object.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace parallax::universe
{

// ---------------------------------------------------------------------------
// QueryFlags
// ---------------------------------------------------------------------------

/// @brief Bitmask controlling which object categories are returned by a query.
enum class QueryFlags : std::uint32_t
{
    None        = 0,
    Stars       = 1u << 0,
    SolarSystem = 1u << 1,
    DeepSky     = 1u << 2,
    Procedural  = 1u << 3,
    All         = Stars | SolarSystem | DeepSky | Procedural,
};

// --- Bitwise operators for QueryFlags ---

[[nodiscard]] constexpr inline QueryFlags operator|(QueryFlags lhs, QueryFlags rhs) noexcept
{
    return static_cast<QueryFlags>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr inline QueryFlags operator&(QueryFlags lhs, QueryFlags rhs) noexcept
{
    return static_cast<QueryFlags>(
        static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr inline QueryFlags operator^(QueryFlags lhs, QueryFlags rhs) noexcept
{
    return static_cast<QueryFlags>(
        static_cast<std::uint32_t>(lhs) ^ static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr inline QueryFlags operator~(QueryFlags v) noexcept
{
    return static_cast<QueryFlags>(~static_cast<std::uint32_t>(v));
}

constexpr inline QueryFlags& operator|=(QueryFlags& lhs, QueryFlags rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

constexpr inline QueryFlags& operator&=(QueryFlags& lhs, QueryFlags rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

constexpr inline QueryFlags& operator^=(QueryFlags& lhs, QueryFlags rhs) noexcept
{
    lhs = lhs ^ rhs;
    return lhs;
}

/// @brief Returns true when @p value contains all bits set in @p flag.
[[nodiscard]] constexpr inline bool has_flag(QueryFlags value, QueryFlags flag) noexcept
{
    return (value & flag) == flag;
}

// ---------------------------------------------------------------------------
// DataProvider
// ---------------------------------------------------------------------------

/// @brief Abstract interface for any source of astronomical objects.
///
/// Concrete implementations (StarCatalogProvider, SolarSystemProvider, etc.)
/// fulfil this contract so the Universe Engine can query them uniformly.
///
/// Copy is disabled; move is defaulted so providers can be stored in unique_ptr
/// and still be movable at the implementation level if needed.
class DataProvider
{
public:
    DataProvider()                               = default;
    DataProvider(const DataProvider&)            = delete;
    DataProvider& operator=(const DataProvider&) = delete;
    DataProvider(DataProvider&&)                 = default;
    DataProvider& operator=(DataProvider&&)      = default;
    virtual ~DataProvider()                      = default;

    /// @brief Fill @p results with all objects inside the given sky cone.
    ///
    /// @param ra          Right ascension of cone centre (radians, J2000).
    /// @param dec         Declination of cone centre (radians, J2000).
    /// @param radius_deg  Half-angle of the query cone (degrees).
    /// @param mag_limit   Faintest magnitude to include (inclusive).
    /// @param flags       Bitmask of object categories to query.
    /// @param results     Output vector — objects are appended, never cleared.
    virtual void query_fov(double ra,
                           double dec,
                           double radius_deg,
                           float  mag_limit,
                           QueryFlags flags,
                           std::vector<CelestialObject>& results) const = 0;

    /// @brief Look up a single object by its packed @c u64 id.
    [[nodiscard]] virtual std::optional<CelestialObject> query_object(u64 id) const = 0;

    /// @brief Total number of objects this provider manages.
    [[nodiscard]] virtual std::size_t get_count() const = 0;
};

} // namespace parallax::universe
