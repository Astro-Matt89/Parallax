#pragma once

/// @file panel_system.hpp
/// @brief Flexible UI panel system for the retro terminal interface.
///
/// Provides anchored panels with semi-transparent dark green backgrounds,
/// green borders, and mouse-over detection. All subsequent UI (toolbar,
/// side panels, info panel) is built on top of this.
///
/// Rect rendering: filled quads for backgrounds, line quads for borders,
/// all batched into minimal draw calls using TRIANGLE_LIST topology.
///
/// SPRINT 05 Task 5.1

#include "core/input.hpp"
#include "core/types.hpp"
#include "vulkan/context.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace parallax::ui
{
    // =================================================================
    // Style
    // =================================================================

    /// @brief Visual style parameters for a panel.
    ///
    /// Default: 1980s observatory CRT terminal aesthetic.
    struct PanelStyle
    {
        Vec4f background = {0.0f, 0.05f, 0.0f, 0.75f};   ///< Dark green-tinted transparent
        Vec4f border     = {0.0f, 0.6f, 0.0f, 0.8f};     ///< Green border
        Vec4f text_color = {0.0f, 1.0f, 0.0f, 1.0f};     ///< Bright green
        Vec4f text_dim   = {0.0f, 0.6f, 0.0f, 0.8f};     ///< Dim green labels
        Vec4f highlight  = {0.0f, 1.0f, 0.0f, 0.3f};     ///< Button hover highlight
        f32 padding      = 8.0f;                           ///< Inner padding (pixels)
        f32 border_width = 1.0f;                           ///< Border thickness (pixels)
    };

    // =================================================================
    // Anchor
    // =================================================================

    /// @brief Anchor position for computing panel screen placement.
    enum class PanelAnchor
    {
        TopLeft,
        TopCenter,
        TopRight,
        MiddleLeft,
        Center,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight
    };

    // =================================================================
    // Panel
    // =================================================================

    /// @brief A single UI panel with anchor, size, visibility, and hit testing.
    ///
    /// Screen position is computed from the anchor + window dimensions each
    /// frame. Content area is the rect inside padding.
    class Panel
    {
    public:
        /// @brief Construct a panel with an identifier, anchor, and pixel size.
        Panel(const std::string& id, PanelAnchor anchor, Vec2f size);

        virtual ~Panel() = default;

        Panel(const Panel&) = delete;
        Panel& operator=(const Panel&) = delete;
        Panel(Panel&&) = default;
        Panel& operator=(Panel&&) = default;

        /// @brief Show or hide this panel.
        void set_visible(bool visible);

        /// @brief Enable or disable mouse dragging.
        void set_draggable(bool draggable);

        /// @brief Set the panel style.
        void set_style(const PanelStyle& style);

        /// @brief Recompute screen position from anchor + viewport dimensions.
        /// @param viewport_width  Window width in pixels.
        /// @param viewport_height Window height in pixels.
        void update_layout(u32 viewport_width, u32 viewport_height);

        /// @brief Check if a screen-space point is inside this panel.
        /// @param screen_pos  Mouse position in pixels (origin = top-left).
        [[nodiscard]] bool contains(Vec2f screen_pos) const;

        /// @brief Get the top-left of the content area (inside padding), in pixels.
        [[nodiscard]] Vec2f get_content_origin() const;

        /// @brief Get the content area size (panel size minus 2× padding), in pixels.
        [[nodiscard]] Vec2f get_content_size() const;

        [[nodiscard]] const std::string& get_id() const;
        [[nodiscard]] bool is_visible() const;
        [[nodiscard]] Vec2f get_position() const;
        [[nodiscard]] Vec2f get_size() const;
        [[nodiscard]] const PanelStyle& get_style() const;

        /// @brief Toggle visibility.
        void toggle_visible();

    protected:
        std::string m_id;
        PanelAnchor m_anchor;
        Vec2f m_position = {0.0f, 0.0f};   ///< Top-left corner in pixels
        Vec2f m_size;                        ///< Width × Height in pixels
        bool m_visible   = true;
        bool m_draggable = false;
        PanelStyle m_style;
    };

    // =================================================================
    // Rect vertex (internal — matches ui_rect.vert)
    // =================================================================

    /// @brief Per-vertex data for filled rectangle rendering.
    struct RectVertex
    {
        Vec2f position;  ///< Screen NDC [-1, 1]
        Vec4f color;     ///< RGBA
    };

    // =================================================================
    // PanelSystem
    // =================================================================

    /// @brief Manages multiple panels: layout, input routing, batched rendering.
    ///
    /// Owns a Vulkan pipeline for filled rectangles (TRIANGLE_LIST, alpha blend)
    /// and a host-visible vertex buffer. Each frame:
    ///   1. Call update_layout() with the current viewport size.
    ///   2. Call process_input() with mouse state.
    ///   3. Inside the render pass, call render_backgrounds() to draw all
    ///      visible panel backgrounds and borders in a single batched draw call.
    class PanelSystem
    {
    public:
        PanelSystem() = default;
        ~PanelSystem();

        PanelSystem(const PanelSystem&) = delete;
        PanelSystem& operator=(const PanelSystem&) = delete;
        PanelSystem(PanelSystem&&) = delete;
        PanelSystem& operator=(PanelSystem&&) = delete;

        /// @brief Create Vulkan resources (pipeline, vertex buffer).
        /// @param context     Vulkan context.
        /// @param render_pass Render pass this will draw into.
        /// @param shader_dir  Directory containing compiled SPIR-V files.
        void init(const vulkan::Context& context,
                  VkRenderPass render_pass,
                  const std::filesystem::path& shader_dir);

        /// @brief Destroy Vulkan resources.
        void destroy();

        /// @brief Register a panel. Ownership transferred.
        void add_panel(std::unique_ptr<Panel> panel);

        /// @brief Find a panel by ID.
        [[nodiscard]] Panel* find_panel(std::string_view id) const;

        /// @brief Recompute all panel positions for the current viewport size.
        void update_layout(u32 viewport_width, u32 viewport_height);

        /// @brief Process mouse input: hover detection (click/drag handled by widgets later).
        void process_input(const core::Input& input, Vec2f mouse_pos);

        /// @brief Render all visible panel backgrounds and borders.
        ///
        /// Must be called inside an active render pass. Issues a single batched
        /// draw call for all panels (filled background quads + border line quads).
        void render_backgrounds(VkCommandBuffer cmd, VkExtent2D extent);

        /// @brief Check if a screen-space point is over any visible panel.
        [[nodiscard]] bool is_mouse_over_ui(Vec2f mouse_pos) const;

        /// @brief Get the currently hovered panel ID (empty if none).
        [[nodiscard]] std::string_view get_hovered_panel_id() const;

    private:
        void create_vertex_buffer();
        void create_pipeline(VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir);

        [[nodiscard]] VkShaderModule create_shader_module(
            const std::filesystem::path& path) const;

        /// @brief Emit 6 vertices (2 triangles) for a filled rectangle.
        void emit_filled_rect(Vec2f pos_px, Vec2f size_px, Vec4f color,
                              f32 vp_w, f32 vp_h);

        /// @brief Emit border quads (4 thin rectangles) around a panel rect.
        void emit_border_rect(Vec2f pos_px, Vec2f size_px, Vec4f color,
                              f32 border_w, f32 vp_w, f32 vp_h);

        /// @brief Convert pixel coordinates to NDC.
        [[nodiscard]] static Vec2f pixel_to_ndc(Vec2f px, f32 vp_w, f32 vp_h);

        // Panels
        std::vector<std::unique_ptr<Panel>> m_panels;
        std::string m_hovered_panel_id;

        // Vulkan resources
        const vulkan::Context* m_context = nullptr;

        VkBuffer m_vertex_buffer       = VK_NULL_HANDLE;
        VkDeviceMemory m_vertex_memory = VK_NULL_HANDLE;
        void* m_mapped_ptr             = nullptr;

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline              = VK_NULL_HANDLE;

        std::vector<RectVertex> m_vertices;

        /// @brief Maximum vertices in the GPU buffer.
        /// 6 verts/filled rect + 24 verts/border = 30 per panel. 256 panels max.
        static constexpr u32 kMaxVertices = 8192;
    };

} // namespace parallax::ui