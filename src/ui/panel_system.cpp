/// @file panel_system.cpp
/// @brief Panel system implementation — layout, hit testing, batched rect rendering.
///
/// SPRINT 05 Task 5.1

#include "ui/panel_system.hpp"

#include "core/logger.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace
{

void check_vk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        PLX_CORE_CRITICAL("Vulkan error in {}: VkResult = {}", operation, static_cast<int>(result));
        std::abort();
    }
}

uint32_t find_memory_type(VkPhysicalDevice physical_device,
                          uint32_t type_filter,
                          VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i)
    {
        if ((type_filter & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    PLX_CORE_CRITICAL("Failed to find suitable memory type");
    std::abort();
}

} // anonymous namespace

namespace parallax::ui
{

// =================================================================
// Panel
// =================================================================

Panel::Panel(const std::string& id, PanelAnchor anchor, Vec2f size)
    : m_id{id}
    , m_anchor{anchor}
    , m_size{size}
{
}

void Panel::set_visible(bool visible) { m_visible = visible; }
void Panel::set_draggable(bool draggable) { m_draggable = draggable; }
void Panel::set_style(const PanelStyle& style) { m_style = style; }

void Panel::toggle_visible() { m_visible = !m_visible; }

const std::string& Panel::get_id() const { return m_id; }
bool Panel::is_visible() const { return m_visible; }
Vec2f Panel::get_position() const { return m_position; }
Vec2f Panel::get_size() const { return m_size; }
const PanelStyle& Panel::get_style() const { return m_style; }

void Panel::update_layout(u32 viewport_width, u32 viewport_height)
{
    const f32 vw = static_cast<f32>(viewport_width);
    const f32 vh = static_cast<f32>(viewport_height);

    // Margin from screen edges
    constexpr f32 kEdgeMargin = 4.0f;

    switch (m_anchor)
    {
    case PanelAnchor::TopLeft:
        m_position = {kEdgeMargin, kEdgeMargin};
        break;
    case PanelAnchor::TopCenter:
        m_position = {(vw - m_size.x) * 0.5f, kEdgeMargin};
        break;
    case PanelAnchor::TopRight:
        m_position = {vw - m_size.x - kEdgeMargin, kEdgeMargin};
        break;
    case PanelAnchor::MiddleLeft:
        m_position = {kEdgeMargin, (vh - m_size.y) * 0.5f};
        break;
    case PanelAnchor::Center:
        m_position = {(vw - m_size.x) * 0.5f, (vh - m_size.y) * 0.5f};
        break;
    case PanelAnchor::MiddleRight:
        m_position = {vw - m_size.x - kEdgeMargin, (vh - m_size.y) * 0.5f};
        break;
    case PanelAnchor::BottomLeft:
        m_position = {kEdgeMargin, vh - m_size.y - kEdgeMargin};
        break;
    case PanelAnchor::BottomCenter:
        m_position = {(vw - m_size.x) * 0.5f, vh - m_size.y - kEdgeMargin};
        break;
    case PanelAnchor::BottomRight:
        m_position = {vw - m_size.x - kEdgeMargin, vh - m_size.y - kEdgeMargin};
        break;
    }
}

bool Panel::contains(Vec2f screen_pos) const
{
    if (!m_visible)
    {
        return false;
    }

    return screen_pos.x >= m_position.x &&
           screen_pos.x <= m_position.x + m_size.x &&
           screen_pos.y >= m_position.y &&
           screen_pos.y <= m_position.y + m_size.y;
}

Vec2f Panel::get_content_origin() const
{
    return {m_position.x + m_style.padding, m_position.y + m_style.padding};
}

Vec2f Panel::get_content_size() const
{
    return {
        std::max(0.0f, m_size.x - 2.0f * m_style.padding),
        std::max(0.0f, m_size.y - 2.0f * m_style.padding)
    };
}

// =================================================================
// PanelSystem — Lifecycle
// =================================================================

PanelSystem::~PanelSystem()
{
    destroy();
}

void PanelSystem::init(const vulkan::Context& context,
                       VkRenderPass render_pass,
                       const std::filesystem::path& shader_dir)
{
    m_context = &context;

    create_vertex_buffer();
    create_pipeline(render_pass, shader_dir);

    PLX_CORE_INFO("PanelSystem initialized ({} max vertices)", kMaxVertices);
}

void PanelSystem::destroy()
{
    if (!m_context)
    {
        return;
    }

    VkDevice device = m_context->get_device();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipeline_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
        m_pipeline_layout = VK_NULL_HANDLE;
    }
    if (m_vertex_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, m_vertex_buffer, nullptr);
        m_vertex_buffer = VK_NULL_HANDLE;
    }
    if (m_vertex_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_vertex_memory, nullptr);
        m_vertex_memory = VK_NULL_HANDLE;
    }

    m_mapped_ptr = nullptr;
    m_context = nullptr;

    PLX_CORE_TRACE("PanelSystem destroyed");
}

// =================================================================
// Panel management
// =================================================================

void PanelSystem::add_panel(std::unique_ptr<Panel> panel)
{
    PLX_CORE_INFO("PanelSystem: added panel '{}'", panel->get_id());
    m_panels.push_back(std::move(panel));
}

Panel* PanelSystem::find_panel(std::string_view id) const
{
    for (const auto& panel : m_panels)
    {
        if (panel->get_id() == id)
        {
            return panel.get();
        }
    }
    return nullptr;
}

// =================================================================
// Layout
// =================================================================

void PanelSystem::update_layout(u32 viewport_width, u32 viewport_height)
{
    for (auto& panel : m_panels)
    {
        panel->update_layout(viewport_width, viewport_height);
    }
}

// =================================================================
// Input
// =================================================================

void PanelSystem::process_input(const core::Input& /*input*/, Vec2f mouse_pos)
{
    // Determine which panel (if any) the mouse is hovering over.
    // Later tasks (5.2) will add click/drag handling for widgets.
    m_hovered_panel_id.clear();

    // Iterate in reverse so top-most panels (added last) get priority
    for (auto it = m_panels.rbegin(); it != m_panels.rend(); ++it)
    {
        if ((*it)->contains(mouse_pos))
        {
            m_hovered_panel_id = (*it)->get_id();
            break;
        }
    }
}

bool PanelSystem::is_mouse_over_ui(Vec2f mouse_pos) const
{
    for (const auto& panel : m_panels)
    {
        if (panel->contains(mouse_pos))
        {
            return true;
        }
    }
    return false;
}

std::string_view PanelSystem::get_hovered_panel_id() const
{
    return m_hovered_panel_id;
}

// =================================================================
// Rendering
// =================================================================

void PanelSystem::render_backgrounds(VkCommandBuffer cmd, VkExtent2D extent)
{
    m_vertices.clear();

    const f32 vp_w = static_cast<f32>(extent.width);
    const f32 vp_h = static_cast<f32>(extent.height);

    for (const auto& panel : m_panels)
    {
        if (!panel->is_visible())
        {
            continue;
        }

        const auto pos = panel->get_position();
        const auto size = panel->get_size();
        const auto& style = panel->get_style();

        // 1. Filled background quad
        emit_filled_rect(pos, size, style.background, vp_w, vp_h);

        // 2. Border (4 thin quads)
        emit_border_rect(pos, size, style.border, style.border_width, vp_w, vp_h);
    }

    if (m_vertices.empty())
    {
        return;
    }

    // Clamp to buffer capacity
    if (m_vertices.size() > kMaxVertices)
    {
        PLX_CORE_WARN("PanelSystem: vertex overflow ({} > {}), truncating",
                      m_vertices.size(), kMaxVertices);
        m_vertices.resize(kMaxVertices);
    }

    // Upload to GPU
    std::memcpy(m_mapped_ptr, m_vertices.data(),
                m_vertices.size() * sizeof(RectVertex));

    // Draw
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertex_buffer, &offset);
    vkCmdDraw(cmd, static_cast<u32>(m_vertices.size()), 1, 0, 0);
}

// =================================================================
// Geometry helpers
// =================================================================

Vec2f PanelSystem::pixel_to_ndc(Vec2f px, f32 vp_w, f32 vp_h)
{
    // Pixel coords: origin top-left, +x right, +y down
    // NDC:          origin center, +x right, +y down (Vulkan convention)
    return {
        (px.x / vp_w) * 2.0f - 1.0f,
        (px.y / vp_h) * 2.0f - 1.0f
    };
}

void PanelSystem::emit_filled_rect(Vec2f pos_px, Vec2f size_px, Vec4f color,
                                   f32 vp_w, f32 vp_h)
{
    const Vec2f tl = pixel_to_ndc(pos_px, vp_w, vp_h);
    const Vec2f br = pixel_to_ndc({pos_px.x + size_px.x, pos_px.y + size_px.y},
                                  vp_w, vp_h);
    const Vec2f tr = {br.x, tl.y};
    const Vec2f bl = {tl.x, br.y};

    // Triangle 1: TL → BL → TR
    m_vertices.push_back({tl, color});
    m_vertices.push_back({bl, color});
    m_vertices.push_back({tr, color});

    // Triangle 2: TR → BL → BR
    m_vertices.push_back({tr, color});
    m_vertices.push_back({bl, color});
    m_vertices.push_back({br, color});
}

void PanelSystem::emit_border_rect(Vec2f pos_px, Vec2f size_px, Vec4f color,
                                   f32 border_w, f32 vp_w, f32 vp_h)
{
    const f32 x0 = pos_px.x;
    const f32 y0 = pos_px.y;
    const f32 x1 = pos_px.x + size_px.x;
    const f32 y1 = pos_px.y + size_px.y;
    const f32 bw = border_w;

    // Top edge
    emit_filled_rect({x0, y0}, {size_px.x, bw}, color, vp_w, vp_h);

    // Bottom edge
    emit_filled_rect({x0, y1 - bw}, {size_px.x, bw}, color, vp_w, vp_h);

    // Left edge (between top and bottom borders)
    emit_filled_rect({x0, y0 + bw}, {bw, size_px.y - 2.0f * bw}, color, vp_w, vp_h);

    // Right edge (between top and bottom borders)
    emit_filled_rect({x1 - bw, y0 + bw}, {bw, size_px.y - 2.0f * bw}, color, vp_w, vp_h);
}

// =================================================================
// Vulkan resource creation
// =================================================================

void PanelSystem::create_vertex_buffer()
{
    const VkDeviceSize buffer_size = sizeof(RectVertex) * kMaxVertices;
    VkDevice device = m_context->get_device();

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &m_vertex_buffer),
             "vkCreateBuffer (panel_system)");

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(device, m_vertex_buffer, &mem_req);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context->get_physical_device(), mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_vertex_memory),
             "vkAllocateMemory (panel_system)");
    check_vk(vkBindBufferMemory(device, m_vertex_buffer, m_vertex_memory, 0),
             "vkBindBufferMemory (panel_system)");
    check_vk(vkMapMemory(device, m_vertex_memory, 0, buffer_size, 0, &m_mapped_ptr),
             "vkMapMemory (panel_system)");
}

void PanelSystem::create_pipeline(VkRenderPass render_pass,
                                  const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context->get_device();

    VkShaderModule vert_module = create_shader_module(shader_dir / "ui_rect.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "ui_rect.frag.spv");

    VkPipelineShaderStageCreateInfo vert_stage{};
    vert_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_stage.module = vert_module;
    vert_stage.pName = "main";

    VkPipelineShaderStageCreateInfo frag_stage{};
    frag_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_stage.module = frag_module;
    frag_stage.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_stage, frag_stage};

    // Vertex input: position (vec2) + color (vec4)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(RectVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attributes[2]{};
    // Position
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(RectVertex, position);
    // Color
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[1].offset = offsetof(RectVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attributes;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Alpha blending: src_alpha, 1 - src_alpha (standard transparency)
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    // Empty pipeline layout (no descriptors, no push constants)
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    check_vk(vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
             "vkCreatePipelineLayout (panel_system)");

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    check_vk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, nullptr, &m_pipeline),
             "vkCreateGraphicsPipelines (panel_system)");

    PLX_CORE_INFO("PanelSystem pipeline created (triangle list, alpha blend)");

    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

VkShaderModule PanelSystem::create_shader_module(
    const std::filesystem::path& path) const
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        PLX_CORE_CRITICAL("Failed to open shader file: {}", path.string());
        std::abort();
    }

    auto file_size = static_cast<std::size_t>(file.tellg());
    if (file_size == 0 || file_size % 4 != 0)
    {
        PLX_CORE_CRITICAL("Invalid SPIR-V file (size {} not aligned to 4): {}",
                          file_size, path.string());
        std::abort();
    }

    std::vector<uint32_t> code(file_size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()),
              static_cast<std::streamsize>(file_size));
    file.close();

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = file_size;
    create_info.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(m_context->get_device(), &create_info, nullptr, &module),
             "vkCreateShaderModule (panel_system)");
    return module;
}

} // namespace parallax::ui