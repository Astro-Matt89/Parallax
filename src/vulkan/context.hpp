#pragma once

/// @file context.hpp
/// @brief Vulkan instance, physical device selection, logical device, and queues.

#include "core/logger.hpp"
#include "core/window.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace parallax::vulkan
{
    /// @brief Configuration for Vulkan context creation.
    struct ContextConfig
    {
        std::string app_name = "Parallax";
        uint32_t app_version = VK_MAKE_API_VERSION(0, 0, 1, 0);
        bool enable_validation = true;
    };

    /// @brief Owns the core Vulkan objects: instance, device, and queues.
    ///
    /// Creates a VkInstance with optional validation layers, obtains a surface
    /// from the Window, selects a physical device (preferring discrete GPUs),
    /// and creates a logical device with graphics and present queue families.
    ///
    /// The constructor takes a Window reference to query required extensions
    /// and create the VkSurfaceKHR after instance creation — resolving the
    /// instance↔surface dependency naturally.
    class Context
    {
    public:
        /// @brief Create the full Vulkan context.
        /// @param config Context configuration.
        /// @param window The application window (used for extensions and surface creation).
        explicit Context(const ContextConfig& config, core::Window& window);

        /// @brief Destroy logical device, debug messenger, surface, and instance (reverse order).
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;
        Context(Context&&) = delete;
        Context& operator=(Context&&) = delete;

        /// @brief The Vulkan instance.
        [[nodiscard]] VkInstance get_instance() const;

        /// @brief The selected physical device (GPU).
        [[nodiscard]] VkPhysicalDevice get_physical_device() const;

        /// @brief The logical device.
        [[nodiscard]] VkDevice get_device() const;

        /// @brief The graphics queue.
        [[nodiscard]] VkQueue get_graphics_queue() const;

        /// @brief The presentation queue.
        [[nodiscard]] VkQueue get_present_queue() const;

        /// @brief Index of the graphics queue family.
        [[nodiscard]] uint32_t get_graphics_queue_family() const;

        /// @brief Index of the present queue family.
        [[nodiscard]] uint32_t get_present_queue_family() const;

        /// @brief The window surface owned by this context.
        [[nodiscard]] VkSurfaceKHR get_surface() const;

        /// @brief Block until all device operations are complete.
        void wait_idle() const;

    private:
        void create_instance(const ContextConfig& config, const std::vector<const char*>& window_extensions);
        void setup_debug_messenger();
        void pick_physical_device();
        void create_logical_device();

        /// @brief Check if a physical device meets all requirements.
        [[nodiscard]] bool is_device_suitable(VkPhysicalDevice device) const;

        /// @brief Score a physical device for ranking (higher = better).
        [[nodiscard]] int rate_device(VkPhysicalDevice device) const;

        /// @brief Log detailed information about the selected GPU.
        void log_device_properties(VkPhysicalDevice device) const;

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphics_queue = VK_NULL_HANDLE;
        VkQueue m_present_queue = VK_NULL_HANDLE;
        uint32_t m_graphics_family = 0;
        uint32_t m_present_family = 0;
        bool m_validation_enabled = false;
    };

} // namespace parallax::vulkan