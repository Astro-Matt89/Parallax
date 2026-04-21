#pragma once

/// @file property_registry.hpp
/// @brief PropertyRegistry — static catalog of all observable properties per object type.

#include "knowledge/property_descriptor.hpp"
#include "universe/object_id.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace parallax::knowledge
{

/// @brief Static registry mapping every ObjectType to its full set of
///        observable PropertyDescriptors.
///
/// All property tables are built once at first use (function-local statics)
/// and are immutable thereafter.  Methods are inherently thread-safe.
///
/// Usage:
/// @code
///   auto props = PropertyRegistry::get_properties(ObjectType::Star);
///   auto pd    = PropertyRegistry::get_property(ObjectType::Star, "mag_v");
///   auto l2    = PropertyRegistry::get_properties_for_level(
///                    ObjectType::Star, KnowledgeLevel::Classified);
/// @endcode
class PropertyRegistry
{
public:
    /// @brief Returns all property descriptors for the given object type.
    ///
    /// The returned span is backed by a static table and is valid for the
    /// lifetime of the program.  Returns an empty span for unknown types.
    [[nodiscard]] static std::span<const PropertyDescriptor>
        get_properties(universe::ObjectType type);

    /// @brief Returns the descriptor for a named property, or std::nullopt.
    ///
    /// Performs a linear scan of the type's property table (tables are small).
    [[nodiscard]] static std::optional<PropertyDescriptor>
        get_property(universe::ObjectType type, std::string_view name);

    /// @brief Returns descriptors whose unlocks_at level exactly equals @p level.
    [[nodiscard]] static std::vector<PropertyDescriptor>
        get_properties_for_level(universe::ObjectType type, KnowledgeLevel level);
};

} // namespace parallax::knowledge
