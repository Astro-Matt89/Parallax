#pragma once

/// @file object_id.hpp
/// @brief Object ID encoding scheme and ObjectType enumeration for the Universe Engine.
///
/// Encoding layout:
///   bits [63..56] — 8-bit type prefix
///   bits [55.. 0] — 56-bit source id

#include <cstdint>

namespace parallax::universe
{

using u64 = std::uint64_t;

// ---------------------------------------------------------------------------
// ObjectType
// ---------------------------------------------------------------------------

/// @brief Classification of every object in the universe (real or procedural).
enum class ObjectType : std::uint8_t
{
    Unknown          = 0x00,
    Star             = 0x01,
    SolarSystemBody  = 0x02,
    DeepSkyObject    = 0x03,
    Galaxy           = 0x04,
    ProceduralStar   = 0x10,
    ProceduralDso    = 0x11,
};

// ---------------------------------------------------------------------------
// Type-prefix constants
// ---------------------------------------------------------------------------

inline constexpr u64 kStarPrefix           = 0x01;
inline constexpr u64 kSolarBodyPrefix      = 0x02;
inline constexpr u64 kDsoPrefix            = 0x03;
inline constexpr u64 kGalaxyPrefix         = 0x04;
inline constexpr u64 kProceduralStarPrefix = 0x10;
inline constexpr u64 kProceduralDsoPrefix  = 0x11;

// ---------------------------------------------------------------------------
// Bit-field parameters
// ---------------------------------------------------------------------------

inline constexpr int  kTypePrefixShift   = 56;
inline constexpr u64  kSourceIdMask      = (u64{1} << kTypePrefixShift) - 1u; // lower 56 bits

// ---------------------------------------------------------------------------
// ID encoding / decoding
// ---------------------------------------------------------------------------

/// @brief Pack a type prefix and a 56-bit source id into a single u64 handle.
[[nodiscard]] constexpr u64 encode_id(ObjectType type, u64 source_id) noexcept
{
    return (static_cast<u64>(type) << kTypePrefixShift) | (source_id & kSourceIdMask);
}

/// @brief Extract the ObjectType from a packed object id.
[[nodiscard]] constexpr ObjectType decode_type(u64 id) noexcept
{
    return static_cast<ObjectType>(id >> kTypePrefixShift);
}

/// @brief Extract the lower 56-bit source id from a packed object id.
[[nodiscard]] constexpr u64 decode_source_id(u64 id) noexcept
{
    return id & kSourceIdMask;
}

} // namespace parallax::universe
