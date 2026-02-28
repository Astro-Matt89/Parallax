#pragma once

/// @file swapchain.hpp
/// @brief Vulkan swapchain management with recreation support.

#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace parallax::vulkan
{
    /// @brief Manages the Vulkan swapchain, its images, and image views.
    ///
    /// Handles format/present mode selection, image view creation, and
    /// full recreation on window resize. The renderer should call recreate()
    /// when acquiring an image returns VK_ERROR_OUT_OF_DATE_KHR or when
    /// the window signals a resize.
    class Swapchain
    {
    public:
        /// @brief Create a swapchain for the given context and window dimensions.
        /// @param context The Vulkan context (device, physical device, surface, queues).
        /// @param width Initial framebuffer width in pixels.
        /// @param height Initial framebuffer height in pixels.
        Swapchain(const Context& context, uint32_t width, uint32_t height);

        /// @brief Destroy image views and the swapchain.
        ~Swapchain();

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;
        Swapchain(Swapchain&&) = delete;
        Swapchain& operator=(Swapchain&&) = delete;

        /// @brief Recreate the swapchain for new window dimensions.
        /// Destroys the old swapchain and image views, then creates new ones.
        /// @param width New framebuffer width in pixels.
        /// @param height New framebuffer height in pixels.
        void recreate(uint32_t width, uint32_t height);

        /// @brief The swapchain handle.
        [[nodiscard]] VkSwapchainKHR get_handle() const;

        /// @brief The chosen surface format.
        [[nodiscard]] VkFormat get_image_format() const;

        /// @brief The actual swapchain extent (may differ from requested).
        [[nodiscard]] VkExtent2D get_extent() const;

        /// @brief The swapchain image views (one per swapchain image).
        [[nodiscard]] const std::vector<VkImageView>& get_image_views() const;

        /// @brief Number of swapchain images.
        [[nodiscard]] uint32_t get_image_count() const;

    private:
        void create(uint32_t width, uint32_t height);
        void destroy();

        /// @brief Choose the best surface format (prefer B8G8R8A8_SRGB + SRGB_NONLINEAR).
        [[nodiscard]] VkSurfaceFormatKHR choose_surface_format(
            const std::vector<VkSurfaceFormatKHR>& available) const;

        /// @brief Choose the best present mode (prefer MAILBOX, fallback FIFO).
        [[nodiscard]] VkPresentModeKHR choose_present_mode(
            const std::vector<VkPresentModeKHR>& available) const;

        /// @brief Clamp the requested extent to surface capabilities.
        [[nodiscard]] VkExtent2D choose_extent(
            const VkSurfaceCapabilitiesKHR& capabilities,
            uint32_t width,
            uint32_t height) const;

        void create_image_views();

        const Context& m_context;

        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkFormat m_image_format = VK_FORMAT_UNDEFINED;
        VkExtent2D m_extent = {0, 0};

        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_image_views;
    };

} // namespace parallax::vulkan