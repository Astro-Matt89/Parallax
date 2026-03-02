/// @file line_renderer.cpp
/// @brief General-purpose GPU line renderer implementation.

#include "rendering/line_renderer.hpp"

#include "core/logger.hpp"

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

namespace parallax::rendering
{

// -----------------------------------------------------------------
// Construction / Destruction
// -----------------------------------------------------------------

LineRenderer::LineRenderer(const vulkan::Context& context,
                           VkRenderPass render_pass,
                           const std::filesystem::path& shader_dir,
                           u32 max_vertices)
    : m_context{context}
{
    m_vertices.reserve(max_vertices);
    create_vertex_buffer(max_vertices);
    create_pipeline(render_pass, shader_dir);

    PLX_CORE_INFO("LineRenderer initialized ({} max vertices)", max_vertices);
}

LineRenderer::~LineRenderer()
{
    VkDevice device = m_context.get_device();

    if (m_pipeline != VK_NULL_HANDLE)        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
    if (m_vertex_buffer != VK_NULL_HANDLE)   vkDestroyBuffer(device, m_vertex_buffer, nullptr);
    if (m_vertex_memory != VK_NULL_HANDLE)   vkFreeMemory(device, m_vertex_memory, nullptr);

    PLX_CORE_TRACE("LineRenderer destroyed");
}

// -----------------------------------------------------------------
// Frame interface
// -----------------------------------------------------------------

void LineRenderer::begin_frame()
{
    m_vertices.clear();
}

void LineRenderer::add_line(Vec2f from, Vec2f to, Vec4f color, f32 /*thickness*/)
{
    if (m_vertices.size() + 2 > m_buffer_capacity)
    {
        return;
    }

    m_vertices.push_back(LineVertex{from, color});
    m_vertices.push_back(LineVertex{to, color});
}

void LineRenderer::add_line_strip(std::span<const Vec2f> points, Vec4f color, f32 /*thickness*/)
{
    if (points.size() < 2)
    {
        return;
    }

    const u32 vertices_needed = static_cast<u32>(points.size() - 1) * 2;
    if (m_vertices.size() + vertices_needed > m_buffer_capacity)
    {
        return;
    }

    for (std::size_t i = 0; i + 1 < points.size(); ++i)
    {
        m_vertices.push_back(LineVertex{points[i],     color});
        m_vertices.push_back(LineVertex{points[i + 1], color});
    }
}

void LineRenderer::add_circle(Vec2f center, f32 radius, Vec4f color,
                               u32 segments, f32 /*thickness*/)
{
    if (segments < 3)
    {
        return;
    }

    const u32 vertices_needed = segments * 2;
    if (m_vertices.size() + vertices_needed > m_buffer_capacity)
    {
        return;
    }

    const f32 step = glm::pi<f32>() * 2.0f / static_cast<f32>(segments);

    for (u32 i = 0; i < segments; ++i)
    {
        const f32 angle_a = static_cast<f32>(i) * step;
        const f32 angle_b = static_cast<f32>(i + 1) * step;

        const Vec2f a = center + Vec2f{std::cos(angle_a), std::sin(angle_a)} * radius;
        const Vec2f b = center + Vec2f{std::cos(angle_b), std::sin(angle_b)} * radius;

        m_vertices.push_back(LineVertex{a, color});
        m_vertices.push_back(LineVertex{b, color});
    }
}

void LineRenderer::render(VkCommandBuffer cmd)
{
    if (m_vertices.empty())
    {
        return;
    }

    upload_vertices();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    VkBuffer buffers[] = {m_vertex_buffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    vkCmdDraw(cmd, static_cast<uint32_t>(m_vertices.size()), 1, 0, 0);
}

u32 LineRenderer::get_vertex_count() const
{
    return static_cast<u32>(m_vertices.size());
}

// -----------------------------------------------------------------
// Vulkan resource creation
// -----------------------------------------------------------------

void LineRenderer::create_vertex_buffer(u32 max_vertices)
{
    m_buffer_capacity = max_vertices;
    const VkDeviceSize buffer_size = sizeof(LineVertex) * max_vertices;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_context.get_device();
    check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &m_vertex_buffer),
             "vkCreateBuffer (line_renderer vertex)");

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, m_vertex_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context.get_physical_device(), mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_vertex_memory),
             "vkAllocateMemory (line_renderer vertex)");
    check_vk(vkBindBufferMemory(device, m_vertex_buffer, m_vertex_memory, 0),
             "vkBindBufferMemory (line_renderer vertex)");
    check_vk(vkMapMemory(device, m_vertex_memory, 0, buffer_size, 0, &m_mapped_ptr),
             "vkMapMemory (line_renderer vertex)");
}

void LineRenderer::create_pipeline(VkRenderPass render_pass,
                                   const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    VkShaderModule vert_module = create_shader_module(shader_dir / "line.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "line.frag.spv");

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

    // Vertex input: binding 0 — one LineVertex per vertex
    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(LineVertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr_descs[2]{};
    attr_descs[0].binding  = 0;
    attr_descs[0].location = 0;
    attr_descs[0].format   = VK_FORMAT_R32G32_SFLOAT;
    attr_descs[0].offset   = offsetof(LineVertex, position);

    attr_descs[1].binding  = 0;
    attr_descs[1].location = 1;
    attr_descs[1].format   = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr_descs[1].offset   = offsetof(LineVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = 2;
    vertex_input.pVertexAttributeDescriptions = attr_descs;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
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

    // Standard alpha blend (NOT additive)
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

    // No descriptor sets, no push constants
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    check_vk(vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
             "vkCreatePipelineLayout (line_renderer)");

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

    check_vk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline),
             "vkCreateGraphicsPipelines (line_renderer)");

    PLX_CORE_INFO("LineRenderer pipeline created (LINE_LIST, standard alpha blend)");

    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

VkShaderModule LineRenderer::create_shader_module(const std::filesystem::path& path) const
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
        PLX_CORE_CRITICAL("Invalid SPIR-V file (size {} not aligned to 4): {}", file_size, path.string());
        std::abort();
    }

    std::vector<uint32_t> code(file_size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(file_size));
    file.close();

    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = file_size;
    create_info.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    check_vk(vkCreateShaderModule(m_context.get_device(), &create_info, nullptr, &module),
             "vkCreateShaderModule (line_renderer)");
    return module;
}

void LineRenderer::upload_vertices()
{
    std::memcpy(m_mapped_ptr, m_vertices.data(), m_vertices.size() * sizeof(LineVertex));
}

} // namespace parallax::rendering
