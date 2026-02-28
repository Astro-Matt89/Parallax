/// @file swapchain.cpp
/// @brief Vulkan swapchain implementation with format, present mode, and recreation.

#include "vulkan/swapchain.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace
{

// -----------------------------------------------------------------
// VkResult checker — logs and aborts on failure
// -----------------------------------------------------------------
void check_vk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        PLX_CORE_CRITICAL("Vulkan error in {}: VkResult = {}", operation, static_cast<int>(result));
        std::abort();
    }
}

} // anonymous namespace

namespace parallax::vulkan
{

Swapchain::Swapchain(const Context& context, uint32_t width, uint32_t height)
    : m_context{context}
{
    create(width, height);
}

Swapchain::~Swapchain()
{
    destroy();
}

void Swapchain::recreate(uint32_t width, uint32_t height)
{
    // Wait for the device to finish using the old swapchain
    check_vk(vkDeviceWaitIdle(m_context.get_device()), "vkDeviceWaitIdle (swapchain recreate)");

    destroy();
    create(width, height);
}

// -----------------------------------------------------------------
// Core creation
// -----------------------------------------------------------------
void Swapchain::create(uint32_t width, uint32_t height)
{
    VkPhysicalDevice physical_device = m_context.get_physical_device();
    VkSurfaceKHR surface = m_context.get_surface();

    // -----------------------------------------------------------------
    // Query surface capabilities
    // -----------------------------------------------------------------
    VkSurfaceCapabilitiesKHR capabilities{};
    check_vk(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    // -----------------------------------------------------------------
    // Query supported formats
    // -----------------------------------------------------------------
    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);

    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());

    // -----------------------------------------------------------------
    // Query supported present modes
    // -----------------------------------------------------------------
    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &present_mode_count, nullptr);

    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        physical_device, surface, &present_mode_count, present_modes.data());

    // -----------------------------------------------------------------
    // Choose best format, present mode, and extent
    // -----------------------------------------------------------------
    VkSurfaceFormatKHR surface_format = choose_surface_format(formats);
    VkPresentModeKHR present_mode = choose_present_mode(present_modes);
    VkExtent2D extent = choose_extent(capabilities, width, height);

    m_image_format = surface_format.format;
    m_extent = extent;

    // -----------------------------------------------------------------
    // Image count: prefer triple buffering, clamped to driver limits
    // -----------------------------------------------------------------
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
    {
        image_count = capabilities.maxImageCount;
    }

    // -----------------------------------------------------------------
    // Create swapchain
    // -----------------------------------------------------------------
    VkSwapchainCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Handle graphics and present queue family divergence
    uint32_t graphics_family = m_context.get_graphics_queue_family();
    uint32_t present_family = m_context.get_present_queue_family();
    uint32_t queue_family_indices[] = {graphics_family, present_family};

    if (graphics_family != present_family)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create_info.queueFamilyIndexCount = 0;
        create_info.pQueueFamilyIndices = nullptr;
    }

    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    check_vk(
        vkCreateSwapchainKHR(m_context.get_device(), &create_info, nullptr, &m_swapchain),
        "vkCreateSwapchainKHR");

    // -----------------------------------------------------------------
    // Retrieve swapchain images
    // -----------------------------------------------------------------
    uint32_t actual_count = 0;
    vkGetSwapchainImagesKHR(m_context.get_device(), m_swapchain, &actual_count, nullptr);
    m_images.resize(actual_count);
    vkGetSwapchainImagesKHR(m_context.get_device(), m_swapchain, &actual_count, m_images.data());

    // -----------------------------------------------------------------
    // Create image views
    // -----------------------------------------------------------------
    create_image_views();

    // -----------------------------------------------------------------
    // Log summary
    // -----------------------------------------------------------------
    const char* present_mode_name =
        present_mode == VK_PRESENT_MODE_MAILBOX_KHR    ? "MAILBOX (triple buffer)"
        : present_mode == VK_PRESENT_MODE_FIFO_KHR     ? "FIFO (vsync)"
        : present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE"
                                                        : "other";

    PLX_CORE_INFO("Swapchain created: {}x{}, {} images, format {}, {}",
                  m_extent.width,
                  m_extent.height,
                  m_images.size(),
                  static_cast<int>(m_image_format),
                  present_mode_name);
}

// -----------------------------------------------------------------
// Destruction
// -----------------------------------------------------------------
void Swapchain::destroy()
{
    VkDevice device = m_context.get_device();

    for (auto view : m_image_views)
    {
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    m_image_views.clear();
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
        PLX_CORE_TRACE("Swapchain destroyed");
    }
}

// -----------------------------------------------------------------
// Format selection: prefer B8G8R8A8_SRGB + SRGB_NONLINEAR
// -----------------------------------------------------------------
VkSurfaceFormatKHR Swapchain::choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& available) const
{
    for (const auto& fmt : available)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB
            && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            PLX_CORE_TRACE("Surface format: B8G8R8A8_SRGB + SRGB_NONLINEAR (preferred)");
            return fmt;
        }
    }

    // Fallback: use whatever the driver gives us first
    PLX_CORE_WARN("Preferred surface format not available — using format {}",
                  static_cast<int>(available[0].format));
    return available[0];
}

// -----------------------------------------------------------------
// Present mode selection: prefer MAILBOX, fallback FIFO
// -----------------------------------------------------------------
VkPresentModeKHR Swapchain::choose_present_mode(
    const std::vector<VkPresentModeKHR>& available) const
{
    for (const auto& mode : available)
    {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            PLX_CORE_TRACE("Present mode: MAILBOX (triple buffering)");
            return mode;
        }
    }

    // FIFO is guaranteed to be available by the Vulkan spec
    PLX_CORE_TRACE("Present mode: FIFO (vsync fallback)");
    return VK_PRESENT_MODE_FIFO_KHR;
}

// -----------------------------------------------------------------
// Extent selection: clamp to surface capabilities
// -----------------------------------------------------------------
VkExtent2D Swapchain::choose_extent(
    const VkSurfaceCapabilitiesKHR& capabilities,
    uint32_t width,
    uint32_t height) const
{
    // If currentExtent is not the special value 0xFFFFFFFF, the driver
    // dictates the exact extent — use it directly.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    // Otherwise clamp our requested size to the allowed range
    VkExtent2D extent{};
    extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

// -----------------------------------------------------------------
// Image view creation
// -----------------------------------------------------------------
void Swapchain::create_image_views()
{
    m_image_views.resize(m_images.size());

    for (std::size_t i = 0; i < m_images.size(); ++i)
    {
        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = m_images[i];
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = m_image_format;
        view_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;

        check_vk(
            vkCreateImageView(m_context.get_device(), &view_info, nullptr, &m_image_views[i]),
            "vkCreateImageView (swapchain)");
    }
}

// -----------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------

VkSwapchainKHR Swapchain::get_handle() const
{
    return m_swapchain;
}

VkFormat Swapchain::get_image_format() const
{
    return m_image_format;
}

VkExtent2D Swapchain::get_extent() const
{
    return m_extent;
}

const std::vector<VkImageView>& Swapchain::get_image_views() const
{
    return m_image_views;
}

uint32_t Swapchain::get_image_count() const
{
    return static_cast<uint32_t>(m_images.size());
}

} // namespace parallax::vulkan