#pragma once

#include "astro/observer.hpp"
#include "core/types.hpp"

#include <span>
#include <vector>

namespace parallax::astro
{
    /// @brief Owns the list of known observer sites and the currently active one.
    ///
    /// SPRINT 09 Task 9.8 — created here so BaseTab can display + mutate it.
    /// Task 9.10 will integrate it into Application and the skychart pipeline.
    ///
    /// For Sprint 09 there is exactly one ObserverRegistry, shared by every
    /// instrument. Sprint 10+ will introduce per-instrument locations.
    class ObserverRegistry
    {
    public:
        ObserverRegistry() = default;
        ~ObserverRegistry() = default;

        ObserverRegistry(const ObserverRegistry&)            = delete;
        ObserverRegistry& operator=(const ObserverRegistry&) = delete;
        ObserverRegistry(ObserverRegistry&&) noexcept        = default;
        ObserverRegistry& operator=(ObserverRegistry&&) noexcept = default;

        /// Create a registry preloaded with the built-in sites
        /// (Tycho Crater + 4 Earth observatories). Tycho Crater is active.
        [[nodiscard]] static ObserverRegistry create_default();

        [[nodiscard]] std::span<const ObserverLocation> get_all() const noexcept;
        [[nodiscard]] u32 size() const noexcept;

        [[nodiscard]] const ObserverLocation& get_active() const noexcept;
        [[nodiscard]] i32 get_active_index() const noexcept;

        /// Set the active location. No-op if @p index is out of range
        /// (logs a warning via spdlog).
        void set_active(i32 index);

        /// Append a new location and return its index. (Not used by BaseTab
        /// yet; included so Sprint 10 can grow the registry.)
        i32 add_location(ObserverLocation location);

    private:
        std::vector<ObserverLocation> m_locations;
        i32 m_active_index = 0;
    };
}
