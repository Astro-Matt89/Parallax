/// @file window.cpp
/// @brief SDL2 window implementation with Vulkan surface support.

#include "core/window.hpp"

namespace parallax::core
{

Window::Window(const WindowConfig& config)
    : m_width{config.width}
    , m_height{config.height}
{
    // -----------------------------------------------------------------
    // Tell SDL we manage our own entry point (no SDL_main hijack)
    // -----------------------------------------------------------------
    SDL_SetMainReady();

    // -----------------------------------------------------------------
    // Initialize SDL video subsystem
    // -----------------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        PLX_CORE_CRITICAL("SDL_Init failed: {}", SDL_GetError());
        return;
    }

    // -----------------------------------------------------------------
    // Assemble window flags
    // -----------------------------------------------------------------
    uint32_t flags = SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN;

    if (config.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    if (config.fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    // -----------------------------------------------------------------
    // Create the SDL2 window
    // -----------------------------------------------------------------
    m_window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        flags);

    if (m_window == nullptr)
    {
        PLX_CORE_CRITICAL("SDL_CreateWindow failed: {}", SDL_GetError());
        return;
    }

    // If fullscreen, query the actual pixel dimensions
    if (config.fullscreen)
    {
        int w = 0;
        int h = 0;
        SDL_GetWindowSize(m_window, &w, &h);
        m_width = static_cast<uint32_t>(w);
        m_height = static_cast<uint32_t>(h);
    }

    PLX_CORE_INFO("Window created: \"{}\" ({}x{}) [Vulkan | {}{}]",
                  config.title,
                  m_width,
                  m_height,
                  config.resizable ? "resizable" : "fixed",
                  config.fullscreen ? " | fullscreen" : "");
}

Window::~Window()
{
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        PLX_CORE_INFO("Window destroyed");
    }

    SDL_Quit();
}

bool Window::should_close() const
{
    return m_should_close;
}

void Window::poll_events()
{
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0)
    {
        switch (event.type)
        {
            case SDL_QUIT:
            {
                m_should_close = true;
                break;
            }

            case SDL_WINDOWEVENT:
            {
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                    {
                        m_should_close = true;
                        break;
                    }

                    case SDL_WINDOWEVENT_RESIZED:
                    {
                        m_width = static_cast<uint32_t>(event.window.data1);
                        m_height = static_cast<uint32_t>(event.window.data2);
                        m_was_resized = true;
                        PLX_CORE_TRACE("Window resized: {}x{}", m_width, m_height);
                        break;
                    }

                    case SDL_WINDOWEVENT_MINIMIZED:
                    {
                        m_width = 0;
                        m_height = 0;
                        m_was_resized = true;
                        PLX_CORE_TRACE("Window minimized");
                        break;
                    }

                    case SDL_WINDOWEVENT_RESTORED:
                    {
                        int w = 0;
                        int h = 0;
                        SDL_GetWindowSize(m_window, &w, &h);
                        m_width = static_cast<uint32_t>(w);
                        m_height = static_cast<uint32_t>(h);
                        m_was_resized = true;
                        PLX_CORE_TRACE("Window restored: {}x{}", m_width, m_height);
                        break;
                    }

                    default:
                        break;
                }
                break;
            }

            default:
                break;
        }
    }
}

SDL_Window* Window::get_native_handle() const
{
    return m_window;
}

std::vector<const char*> Window::get_required_vulkan_extensions() const
{
    uint32_t count = 0;
    if (SDL_Vulkan_GetInstanceExtensions(m_window, &count, nullptr) == SDL_FALSE)
    {
        PLX_CORE_ERROR("SDL_Vulkan_GetInstanceExtensions (count) failed: {}", SDL_GetError());
        return {};
    }

    std::vector<const char*> extensions(count);
    if (SDL_Vulkan_GetInstanceExtensions(m_window, &count, extensions.data()) == SDL_FALSE)
    {
        PLX_CORE_ERROR("SDL_Vulkan_GetInstanceExtensions (names) failed: {}", SDL_GetError());
        return {};
    }

    PLX_CORE_TRACE("SDL2 requires {} Vulkan instance extension(s):", count);
    for (const auto* ext : extensions)
    {
        PLX_CORE_TRACE("  - {}", ext);
    }

    return extensions;
}

VkSurfaceKHR Window::create_vulkan_surface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    if (SDL_Vulkan_CreateSurface(m_window, instance, &surface) == SDL_FALSE)
    {
        PLX_CORE_CRITICAL("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return VK_NULL_HANDLE;
    }

    PLX_CORE_INFO("Vulkan surface created");
    return surface;
}

uint32_t Window::get_width() const
{
    return m_width;
}

uint32_t Window::get_height() const
{
    return m_height;
}

bool Window::was_resized()
{
    bool resized = m_was_resized;
    m_was_resized = false;
    return resized;
}

} // namespace parallax::core