# Sprint 01 — Foundation

**Goal:** Establish the Vulkan rendering pipeline, SDL2 window, and verify GPU output.
**Deliverable:** A window displaying a black sky with a single rendered test star (white point).

---

## Prerequisites

Before starting:
- Install Vulkan SDK (LunarG)
- Install vcpkg and bootstrap it
- Ensure CMake 3.25+ is available
- Verify `vulkaninfo` runs on target machine

---

## Tasks

### Task 1.1 — Project Scaffold

Create the CMake project structure with vcpkg integration.

**Files to create:**
- `CMakeLists.txt` (root)
- `vcpkg.json`
- `src/CMakeLists.txt`
- `.clang-format`

**CMake requirements:**
- C++20 standard enforced
- vcpkg toolchain file integration
- Find packages: Vulkan, SDL2, glm, spdlog
- Compile shaders as custom build step (GLSL → SPIR-V)
- Output binary to `build/bin/`
- Platform detection: Windows (MSVC) / Linux (GCC/Clang)

**vcpkg.json dependencies:**
```json
{
  "name": "parallax",
  "version-string": "0.1.0",
  "dependencies": [
    "sdl2",
    "glm",
    "spdlog",
    "vulkan-memory-allocator"
  ]
}
```

Note: VulkanMemoryAllocator (VMA) added here — essential for sane Vulkan memory management.

**.clang-format:**
- BasedOnStyle: LLVM
- IndentWidth: 4
- BreakBeforeBraces: Allman
- ColumnLimit: 120
- PointerAlignment: Left
- AllowShortFunctionsOnASingleLine: None

**Acceptance:** `cmake --preset default && cmake --build build` compiles with zero errors.

---

### Task 1.2 — Core: Logger

Wrap spdlog into a project-specific logger.

**File:** `src/core/logger.hpp`, `src/core/logger.cpp`

**Interface:**
```cpp
namespace parallax::core
{
    class Logger
    {
    public:
        static void init();
        static void shutdown();

        static std::shared_ptr<spdlog::logger>& get_core_logger();
        static std::shared_ptr<spdlog::logger>& get_app_logger();
    };
}

// Convenience macros
#define PLX_CORE_TRACE(...)    ::parallax::core::Logger::get_core_logger()->trace(__VA_ARGS__)
#define PLX_CORE_INFO(...)     ::parallax::core::Logger::get_core_logger()->info(__VA_ARGS__)
#define PLX_CORE_WARN(...)     ::parallax::core::Logger::get_core_logger()->warn(__VA_ARGS__)
#define PLX_CORE_ERROR(...)    ::parallax::core::Logger::get_core_logger()->error(__VA_ARGS__)
#define PLX_CORE_CRITICAL(...) ::parallax::core::Logger::get_core_logger()->critical(__VA_ARGS__)

#define PLX_TRACE(...)         ::parallax::core::Logger::get_app_logger()->trace(__VA_ARGS__)
#define PLX_INFO(...)          ::parallax::core::Logger::get_app_logger()->info(__VA_ARGS__)
#define PLX_WARN(...)          ::parallax::core::Logger::get_app_logger()->warn(__VA_ARGS__)
#define PLX_ERROR(...)         ::parallax::core::Logger::get_app_logger()->error(__VA_ARGS__)
#define PLX_CRITICAL(...)      ::parallax::core::Logger::get_app_logger()->critical(__VA_ARGS__)
```

**Details:**
- Two loggers: `PARALLAX` (engine) and `APP` (gameplay)
- Console sink with color
- File sink to `parallax.log` (rotating, 5MB max, 3 files)
- Pattern: `[%T.%e] [%n] [%^%l%$] %v`
- Init called once from `main()`

**Acceptance:** `PLX_CORE_INFO("Parallax v0.1.0");` prints to console and file.

---

### Task 1.3 — Core: Window

SDL2 window with Vulkan surface support.

**File:** `src/core/window.hpp`, `src/core/window.cpp`

**Interface:**
```cpp
namespace parallax::core
{
    struct WindowConfig
    {
        std::string title = "Parallax";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool fullscreen = false;
        bool resizable = true;
    };

    class Window
    {
    public:
        explicit Window(const WindowConfig& config);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        [[nodiscard]] bool should_close() const;
        void poll_events();

        [[nodiscard]] SDL_Window* get_native_handle() const;
        [[nodiscard]] std::vector<const char*> get_required_vulkan_extensions() const;
        [[nodiscard]] VkSurfaceKHR create_vulkan_surface(VkInstance instance) const;

        [[nodiscard]] uint32_t get_width() const;
        [[nodiscard]] uint32_t get_height() const;

    private:
        SDL_Window* m_window = nullptr;
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        bool m_should_close = false;
    };
}
```

**Details:**
- `SDL_Init(SDL_INIT_VIDEO)` in constructor
- `SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE` flags
- Handle `SDL_QUIT` and `SDL_WINDOWEVENT_CLOSE` in `poll_events()`
- Handle `SDL_WINDOWEVENT_RESIZED` → update m_width/m_height + flag for swapchain recreation
- Log window creation info

**Acceptance:** Window opens, stays open, closes cleanly on X button.

---

### Task 1.4 — Vulkan: Context

Vulkan instance, physical device selection, logical device, and queues.

**File:** `src/vulkan/context.hpp`, `src/vulkan/context.cpp`

**Interface:**
```cpp
namespace parallax::vulkan
{
    struct ContextConfig
    {
        std::string app_name = "Parallax";
        uint32_t app_version = VK_MAKE_VERSION(0, 1, 0);
        bool enable_validation = true;   // debug builds
        std::vector<const char*> required_extensions;
    };

    class Context
    {
    public:
        explicit Context(const ContextConfig& config);
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        [[nodiscard]] VkInstance get_instance() const;
        [[nodiscard]] VkPhysicalDevice get_physical_device() const;
        [[nodiscard]] VkDevice get_device() const;
        [[nodiscard]] VkQueue get_graphics_queue() const;
        [[nodiscard]] VkQueue get_present_queue() const;
        [[nodiscard]] uint32_t get_graphics_queue_family() const;
        [[nodiscard]] uint32_t get_present_queue_family() const;

        void wait_idle() const;

    private:
        void create_instance(const ContextConfig& config);
        void setup_debug_messenger();
        void pick_physical_device();
        void create_logical_device();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        VkQueue m_graphics_queue = VK_NULL_HANDLE;
        VkQueue m_present_queue = VK_NULL_HANDLE;
        uint32_t m_graphics_family = 0;
        uint32_t m_present_family = 0;
    };
}
```

**Details:**
- Validation layers enabled in debug builds (`VK_LAYER_KHRONOS_validation`)
- Debug messenger logs Vulkan messages through spdlog
- Physical device selection: prefer discrete GPU, require graphics + present queues
- Required device extensions: `VK_KHR_swapchain`
- Log: GPU name, driver version, API version, memory size

**Acceptance:** Prints GPU info to log, no validation errors.

---

### Task 1.5 — Vulkan: Swapchain

Swapchain creation with recreation support.

**File:** `src/vulkan/swapchain.hpp`, `src/vulkan/swapchain.cpp`

**Details:**
- Prefer `VK_FORMAT_B8G8R8A8_SRGB` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
- Prefer `VK_PRESENT_MODE_MAILBOX_KHR`, fallback to `FIFO`
- Handle window resize → recreate swapchain
- Retrieve swapchain images and create `VkImageView` for each
- Triple buffering if available

**Acceptance:** Swapchain created, image views valid, recreation works on resize.

---

### Task 1.6 — Vulkan: Render Pass + Pipeline

Minimal graphics pipeline for test rendering.

**Files:**
- `src/vulkan/pipeline.hpp/cpp`
- `shaders/test_star.vert`
- `shaders/test_star.frag`

**Vertex shader:** Hardcoded single point at screen center (no vertex buffer needed).
**Fragment shader:** Output white pixel.

**Pipeline config:**
- Topology: `VK_PRIMITIVE_TOPOLOGY_POINT_LIST`
- Point size: 3.0 (via `gl_PointSize`)
- No depth test
- No blending (yet)
- Viewport and scissor: dynamic state

**Acceptance:** Single white dot rendered at screen center.

---

### Task 1.7 — Core: Application

Main loop tying everything together.

**File:** `src/core/application.hpp`, `src/core/application.cpp`, `src/main.cpp`

**Interface:**
```cpp
namespace parallax::core
{
    class Application
    {
    public:
        Application();
        ~Application();

        void run();

    private:
        void init();
        void main_loop();
        void shutdown();

        void draw_frame();

        std::unique_ptr<Window> m_window;
        std::unique_ptr<vulkan::Context> m_context;
        std::unique_ptr<vulkan::Swapchain> m_swapchain;
        // ... render pass, pipeline, command buffers, sync objects
    };
}
```

**main.cpp:**
```cpp
#include "core/application.hpp"
#include "core/logger.hpp"

int main()
{
    parallax::core::Logger::init();
    PLX_CORE_INFO("Parallax v0.1.0 starting...");

    {
        parallax::core::Application app;
        app.run();
    }

    PLX_CORE_INFO("Parallax shutdown complete.");
    parallax::core::Logger::shutdown();
    return 0;
}
```

**Frame loop:**
1. `poll_events()`
2. Acquire swapchain image
3. Record command buffer (clear black + draw test point)
4. Submit command buffer
5. Present
6. Handle swapchain out-of-date

**Sync:** 2 frames in flight (semaphores + fences).

**Acceptance:** Black window with white dot at center, clean exit, no validation errors, 60fps vsync.

---

## Task Order

```
1.1 → 1.2 → 1.3 → 1.4 → 1.5 → 1.6 → 1.7
(scaffold) (log) (window) (vulkan) (swap) (pipe) (app)
```

Each task builds on the previous. No task should be skipped.

---

## Definition of Done — Sprint 01

- [ ] Project compiles on Windows (MSVC) and Linux (GCC)
- [ ] Window opens with title "Parallax"
- [ ] Black background rendered (sky)
- [ ] Single white point rendered (test star)
- [ ] Console shows GPU info and init logs
- [ ] Clean shutdown, no leaks, no validation errors
- [ ] Window resize works (swapchain recreation)
- [ ] All code follows naming conventions from CLAUDE.md
