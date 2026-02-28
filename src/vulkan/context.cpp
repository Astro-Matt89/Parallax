/// @file context.cpp
/// @brief Vulkan context implementation — instance, device selection, logical device.

#include "vulkan/context.hpp"

#include <cstdlib>
#include <optional>
#include <set>
#include <vector>

namespace
{

// -----------------------------------------------------------------
// Constants
// -----------------------------------------------------------------
constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

constexpr const char* kRequiredDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

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

// -----------------------------------------------------------------
// Debug messenger callback — routes Vulkan messages through spdlog
// -----------------------------------------------------------------
VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    [[maybe_unused]] void* user_data)
{
    switch (severity)
    {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            PLX_CORE_TRACE("[Vulkan] {}", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            PLX_CORE_INFO("[Vulkan] {}", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            PLX_CORE_WARN("[Vulkan] {}", callback_data->pMessage);
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            PLX_CORE_ERROR("[Vulkan] {}", callback_data->pMessage);
            break;
        default:
            PLX_CORE_WARN("[Vulkan] (unknown severity) {}", callback_data->pMessage);
            break;
    }

    return VK_FALSE;
}

// -----------------------------------------------------------------
// Proxy functions for debug utils extension (not loaded by default)
// -----------------------------------------------------------------
VkResult create_debug_utils_messenger(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* create_info,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* messenger)
{
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func != nullptr)
    {
        return func(instance, create_info, allocator, messenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroy_debug_utils_messenger(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* allocator)
{
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != nullptr)
    {
        func(instance, messenger, allocator);
    }
}

// -----------------------------------------------------------------
// Check if validation layer is available
// -----------------------------------------------------------------
bool check_validation_layer_support()
{
    uint32_t layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);

    std::vector<VkLayerProperties> available_layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

    for (const auto& layer : available_layers)
    {
        if (std::string_view{layer.layerName} == kValidationLayerName)
        {
            return true;
        }
    }

    return false;
}

// -----------------------------------------------------------------
// Populate debug messenger create info (reused for instance creation)
// -----------------------------------------------------------------
VkDebugUtilsMessengerCreateInfoEXT make_debug_messenger_create_info()
{
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                         | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debug_messenger_callback;
    info.pUserData = nullptr;
    return info;
}

// -----------------------------------------------------------------
// Queue family lookup helpers
// -----------------------------------------------------------------
struct QueueFamilyIndices
{
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;

    [[nodiscard]] bool is_complete() const
    {
        return graphics.has_value() && present.has_value();
    }
};

QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface)
{
    QueueFamilyIndices indices;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            indices.graphics = i;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support == VK_TRUE)
        {
            indices.present = i;
        }

        if (indices.is_complete())
        {
            break;
        }
    }

    return indices;
}

// -----------------------------------------------------------------
// Check device extension support
// -----------------------------------------------------------------
bool check_device_extension_support(VkPhysicalDevice device)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);

    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    for (const auto* required : kRequiredDeviceExtensions)
    {
        bool found = false;
        for (const auto& ext : available)
        {
            if (std::string_view{ext.extensionName} == required)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
    }

    return true;
}

} // anonymous namespace

// =================================================================
// parallax::vulkan::Context implementation
// =================================================================

namespace parallax::vulkan
{

Context::Context(const ContextConfig& config, core::Window& window)
{
    // 1. Create instance (needs window extensions)
    auto window_extensions = window.get_required_vulkan_extensions();
    create_instance(config, window_extensions);

    // 2. Debug messenger (uses instance)
    if (m_validation_enabled)
    {
        setup_debug_messenger();
    }

    // 3. Create surface (needs instance + window)
    m_surface = window.create_vulkan_surface(m_instance);
    if (m_surface == VK_NULL_HANDLE)
    {
        PLX_CORE_CRITICAL("Failed to create Vulkan surface");
        std::abort();
    }

    // 4. Pick physical device (needs instance + surface for queue families)
    pick_physical_device();

    // 5. Create logical device (needs physical device + queue families)
    create_logical_device();
}

Context::~Context()
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, nullptr);
        PLX_CORE_TRACE("Vulkan logical device destroyed");
    }

    if (m_validation_enabled && m_debug_messenger != VK_NULL_HANDLE)
    {
        destroy_debug_utils_messenger(m_instance, m_debug_messenger, nullptr);
        PLX_CORE_TRACE("Vulkan debug messenger destroyed");
    }

    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        PLX_CORE_TRACE("Vulkan surface destroyed");
    }

    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        PLX_CORE_TRACE("Vulkan instance destroyed");
    }
}

// -----------------------------------------------------------------
// Instance creation
// -----------------------------------------------------------------
void Context::create_instance(const ContextConfig& config,
                              const std::vector<const char*>& window_extensions)
{
    m_validation_enabled = config.enable_validation;
    if (m_validation_enabled && !check_validation_layer_support())
    {
        PLX_CORE_WARN("Validation layers requested but not available — disabling");
        m_validation_enabled = false;
    }

    // Application info
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = config.app_name.c_str();
    app_info.applicationVersion = config.app_version;
    app_info.pEngineName = "Parallax";
    app_info.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app_info.apiVersion = VK_API_VERSION_1_3;

    // Instance extensions = window extensions + optional debug
    std::vector<const char*> extensions = window_extensions;
    if (m_validation_enabled)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    PLX_CORE_INFO("Requesting {} instance extension(s):", extensions.size());
    for (const auto* ext : extensions)
    {
        PLX_CORE_INFO("  - {}", ext);
    }

    // Validation layers
    std::vector<const char*> layers;
    if (m_validation_enabled)
    {
        layers.push_back(kValidationLayerName);
        PLX_CORE_INFO("Validation layers enabled: {}", kValidationLayerName);
    }

    // Create info
    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create_info.ppEnabledExtensionNames = extensions.data();
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    // Chain debug messenger for instance creation/destruction messages
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = make_debug_messenger_create_info();
    if (m_validation_enabled)
    {
        create_info.pNext = &debug_create_info;
    }

    VkResult result = vkCreateInstance(&create_info, nullptr, &m_instance);
    check_vk(result, "vkCreateInstance");

    PLX_CORE_INFO("Vulkan instance created (API 1.3)");
}

// -----------------------------------------------------------------
// Debug messenger
// -----------------------------------------------------------------
void Context::setup_debug_messenger()
{
    VkDebugUtilsMessengerCreateInfoEXT create_info = make_debug_messenger_create_info();

    VkResult result = create_debug_utils_messenger(m_instance, &create_info, nullptr, &m_debug_messenger);
    check_vk(result, "vkCreateDebugUtilsMessengerEXT");

    PLX_CORE_INFO("Vulkan debug messenger created");
}

// -----------------------------------------------------------------
// Physical device selection
// -----------------------------------------------------------------
void Context::pick_physical_device()
{
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);

    if (device_count == 0)
    {
        PLX_CORE_CRITICAL("No Vulkan-capable GPUs found");
        std::abort();
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());

    PLX_CORE_INFO("Found {} Vulkan-capable GPU(s):", device_count);

    int best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;

    for (const auto& device : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        PLX_CORE_INFO("  - {} (type: {})",
                      props.deviceName,
                      props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? "discrete"
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "integrated"
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? "virtual"
                      : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? "CPU"
                                                                                   : "other");

        if (!is_device_suitable(device))
        {
            continue;
        }

        int score = rate_device(device);
        if (score > best_score)
        {
            best_score = score;
            best_device = device;
        }
    }

    if (best_device == VK_NULL_HANDLE)
    {
        PLX_CORE_CRITICAL("No suitable GPU found (need graphics + present queues + swapchain)");
        std::abort();
    }

    m_physical_device = best_device;

    auto indices = find_queue_families(m_physical_device, m_surface);
    m_graphics_family = indices.graphics.value();
    m_present_family = indices.present.value();

    log_device_properties(m_physical_device);
}

bool Context::is_device_suitable(VkPhysicalDevice device) const
{
    auto indices = find_queue_families(device, m_surface);
    if (!indices.is_complete())
    {
        return false;
    }

    if (!check_device_extension_support(device))
    {
        return false;
    }

    uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, nullptr);

    uint32_t present_mode_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &present_mode_count, nullptr);

    return (format_count > 0) && (present_mode_count > 0);
}

int Context::rate_device(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    int score = 0;

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
    {
        score += 10000;
    }
    else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
    {
        score += 1000;
    }

    score += static_cast<int>(props.limits.maxImageDimension2D);

    auto indices = find_queue_families(device, m_surface);
    if (indices.graphics == indices.present)
    {
        score += 500;
    }

    return score;
}

void Context::log_device_properties(VkPhysicalDevice device) const
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(device, &mem_props);

    uint64_t vram_bytes = 0;
    for (uint32_t i = 0; i < mem_props.memoryHeapCount; ++i)
    {
        if ((mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
        {
            vram_bytes += mem_props.memoryHeaps[i].size;
        }
    }

    uint32_t driver_major = VK_API_VERSION_MAJOR(props.driverVersion);
    uint32_t driver_minor = VK_API_VERSION_MINOR(props.driverVersion);
    uint32_t driver_patch = VK_API_VERSION_PATCH(props.driverVersion);

    uint32_t api_major = VK_API_VERSION_MAJOR(props.apiVersion);
    uint32_t api_minor = VK_API_VERSION_MINOR(props.apiVersion);
    uint32_t api_patch = VK_API_VERSION_PATCH(props.apiVersion);

    PLX_CORE_INFO("Selected GPU: {}", props.deviceName);
    PLX_CORE_INFO("  Driver version: {}.{}.{}", driver_major, driver_minor, driver_patch);
    PLX_CORE_INFO("  Vulkan API: {}.{}.{}", api_major, api_minor, api_patch);
    PLX_CORE_INFO("  VRAM: {} MB", vram_bytes / (1024 * 1024));
    PLX_CORE_INFO("  Graphics queue family: {}", m_graphics_family);
    PLX_CORE_INFO("  Present queue family: {}", m_present_family);
}

// -----------------------------------------------------------------
// Logical device creation
// -----------------------------------------------------------------
void Context::create_logical_device()
{
    std::set<uint32_t> unique_families = {m_graphics_family, m_present_family};

    constexpr float kQueuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(unique_families.size());

    for (uint32_t family : unique_families)
    {
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &kQueuePriority;
        queue_create_infos.push_back(queue_info);
    }

    VkPhysicalDeviceFeatures device_features{};

    VkDeviceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
    create_info.pQueueCreateInfos = queue_create_infos.data();
    create_info.pEnabledFeatures = &device_features;
    create_info.enabledExtensionCount = static_cast<uint32_t>(std::size(kRequiredDeviceExtensions));
    create_info.ppEnabledExtensionNames = kRequiredDeviceExtensions;

    // Deprecated but some older drivers still look at device-level layers
    std::vector<const char*> layers;
    if (m_validation_enabled)
    {
        layers.push_back(kValidationLayerName);
    }
    create_info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    create_info.ppEnabledLayerNames = layers.data();

    VkResult result = vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device);
    check_vk(result, "vkCreateDevice");

    vkGetDeviceQueue(m_device, m_graphics_family, 0, &m_graphics_queue);
    vkGetDeviceQueue(m_device, m_present_family, 0, &m_present_queue);

    PLX_CORE_INFO("Vulkan logical device created");
}

// -----------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------

VkInstance Context::get_instance() const
{
    return m_instance;
}

VkPhysicalDevice Context::get_physical_device() const
{
    return m_physical_device;
}

VkDevice Context::get_device() const
{
    return m_device;
}

VkQueue Context::get_graphics_queue() const
{
    return m_graphics_queue;
}

VkQueue Context::get_present_queue() const
{
    return m_present_queue;
}

uint32_t Context::get_graphics_queue_family() const
{
    return m_graphics_family;
}

uint32_t Context::get_present_queue_family() const
{
    return m_present_family;
}

VkSurfaceKHR Context::get_surface() const
{
    return m_surface;
}

void Context::wait_idle() const
{
    if (m_device != VK_NULL_HANDLE)
    {
        check_vk(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
    }
}

} // namespace parallax::vulkan