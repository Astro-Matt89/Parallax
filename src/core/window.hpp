#pragma once

/// @file window.hpp
/// @brief SDL2 window with Vulkan surface support.

#include "core/logger.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace parallax::core
{
    /// @brief Configuration for window creation.
    /// Use designated initializers: Window w({.title = "Parallax", .width = 1920});
    struct WindowConfig
    {
        std::string title = "Parallax";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool fullscreen = false;
        bool resizable = true;
    };

    /// @brief Callback type for receiving raw SDL events from the window.
    using EventCallback = std::function<void(const SDL_Event&)>;

    /// @brief SDL2 window wrapper providing Vulkan surface creation and input polling.
    ///
    /// Owns the SDL_Window lifetime. Handles SDL_QUIT, window close, and resize events.
    /// Non-copyable — exactly one window instance should exist.
    class Window
    {
    public:
        /// @brief Create and show an SDL2 window with Vulkan support.
        /// @param config Window configuration (title, dimensions, flags).
        explicit Window(const WindowConfig& config);

        /// @brief Destroy the SDL2 window and shut down the SDL video subsystem.
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        /// @brief Returns true if the window has been requested to close.
        [[nodiscard]] bool should_close() const;

        /// @brief Request the window to close (e.g., from Escape key).
        void request_close();

        /// @brief Poll all pending SDL events.
        /// Updates internal state for close requests and resize events.
        /// If an event callback is set, it is called for every event.
        void poll_events();

        /// @brief Set a callback to receive all SDL events during poll_events().
        /// The callback is invoked for every event, including window events.
        /// @param callback The callback function, or nullptr to clear.
        void set_event_callback(EventCallback callback);

        /// @brief Access the underlying SDL_Window pointer.
        [[nodiscard]] SDL_Window* get_native_handle() const;

        /// @brief Query Vulkan instance extensions required by SDL2 for surface creation.
        /// @return Vector of extension name C-strings (lifetime managed by SDL2).
        [[nodiscard]] std::vector<const char*> get_required_vulkan_extensions() const;

        /// @brief Create a Vulkan surface for this window.
        /// @param instance A valid VkInstance.
        /// @return The created VkSurfaceKHR. Caller is responsible for destroying it.
        [[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance) const;

        /// @brief Current framebuffer width in pixels.
        [[nodiscard]] uint32_t get_width() const;

        /// @brief Current framebuffer height in pixels.
        [[nodiscard]] uint32_t get_height() const;

        /// @brief Returns true if the window was resized since the last call to poll_events().
        /// Resets the flag after reading.
        [[nodiscard]] bool was_resized();

    private:
        SDL_Window* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_should_close = false;
        bool m_was_resized = false;
        EventCallback m_event_callback;
    };

} // namespace parallax::core