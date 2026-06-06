#include "astro/observer_registry.hpp"

#include <glm/trigonometric.hpp>
#include <spdlog/spdlog.h>

#include <cassert>
#include <utility>

namespace parallax::astro
{
    ObserverRegistry ObserverRegistry::create_default()
    {
        // TODO(Sprint 09 Task 9.10): integrate ObserverRegistry into Application and PlanetariumTab.
        ObserverRegistry registry;

        registry.add_location(ObserverLocation{
            .latitude_rad = glm::radians(-43.31),
            .longitude_rad = glm::radians(-11.36),
            .elevation_m = 0.0,
            .name = "Tycho Crater Base",
            .parent_body = ParentBody::Moon,
            .has_atmosphere = false,
            .bortle_scale = 0.0f
        });

        registry.add_location(ObserverLocation{
            .latitude_rad = glm::radians(28.7570),
            .longitude_rad = glm::radians(-17.8856),
            .elevation_m = 2396.0,
            .name = "La Palma",
            .parent_body = ParentBody::Earth,
            .has_atmosphere = true,
            .bortle_scale = 2.0f
        });

        registry.add_location(ObserverLocation{
            .latitude_rad = glm::radians(19.8207),
            .longitude_rad = glm::radians(-155.4681),
            .elevation_m = 4205.0,
            .name = "Mauna Kea",
            .parent_body = ParentBody::Earth,
            .has_atmosphere = true,
            .bortle_scale = 2.0f
        });

        registry.add_location(ObserverLocation{
            .latitude_rad = glm::radians(-24.6275),
            .longitude_rad = glm::radians(-70.4044),
            .elevation_m = 2635.0,
            .name = "Paranal",
            .parent_body = ParentBody::Earth,
            .has_atmosphere = true,
            .bortle_scale = 1.0f
        });

        registry.add_location(ObserverLocation{
            .latitude_rad = glm::radians(30.6797),
            .longitude_rad = glm::radians(-104.0247),
            .elevation_m = 2070.0,
            .name = "McDonald",
            .parent_body = ParentBody::Earth,
            .has_atmosphere = true,
            .bortle_scale = 2.0f
        });

        registry.m_active_index = 0;
        return registry;
    }

    std::span<const ObserverLocation> ObserverRegistry::get_all() const noexcept
    {
        return m_locations;
    }

    u32 ObserverRegistry::size() const noexcept
    {
        return static_cast<u32>(m_locations.size());
    }

    const ObserverLocation& ObserverRegistry::get_active() const noexcept
    {
        assert(!m_locations.empty());
        return m_locations[static_cast<std::size_t>(m_active_index)];
    }

    i32 ObserverRegistry::get_active_index() const noexcept
    {
        return m_active_index;
    }

    void ObserverRegistry::set_active(const i32 index)
    {
        if (index < 0 || index >= static_cast<i32>(m_locations.size()))
        {
            spdlog::warn("ObserverRegistry::set_active out of range index={} size={}",
                         index,
                         m_locations.size());
            return;
        }

        m_active_index = index;
    }

    i32 ObserverRegistry::add_location(ObserverLocation location)
    {
        m_locations.push_back(std::move(location));
        return static_cast<i32>(m_locations.size()) - 1;
    }
}
