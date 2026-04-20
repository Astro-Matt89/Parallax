/// @file starfield.cpp
/// @brief Starfield renderer implementation: skychart mode.
///
/// No atmospheric effects. Stars are rendered with their catalog magnitude
/// and B-V color. Horizon culling (alt < 0°) is the only physical filter.
///
/// Uses Coordinates::project_radec_to_screen() — the SAME shared function
/// as constellation overlays — to guarantee identical screen positions.

#include "rendering/starfield.hpp"

#include "core/logger.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
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

Starfield::Starfield(const vulkan::Context& context,
                     VkRenderPass render_pass,
                     const std::filesystem::path& shader_dir,
                     u32 max_stars)
    : m_context{context}
{
    create_storage_buffer(max_stars);
    create_descriptor_set_layout();
    create_descriptor_pool_and_set();
    create_pipeline(render_pass, shader_dir);

    PLX_CORE_INFO("Starfield renderer initialized (skychart mode, {} max stars)", max_stars);
}

Starfield::~Starfield()
{
    VkDevice device = m_context.get_device();

    if (m_pipeline != VK_NULL_HANDLE)        vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_pipeline_layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
    if (m_descriptor_pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, m_descriptor_pool, nullptr);
    if (m_descriptor_set_layout != VK_NULL_HANDLE)
        vkDestroyDescriptorSetLayout(device, m_descriptor_set_layout, nullptr);
    if (m_storage_buffer != VK_NULL_HANDLE)  vkDestroyBuffer(device, m_storage_buffer, nullptr);
    if (m_storage_memory != VK_NULL_HANDLE)  vkFreeMemory(device, m_storage_memory, nullptr);

    PLX_CORE_TRACE("Starfield renderer destroyed");
}

// -----------------------------------------------------------------
// begin_frame() / add_celestial_object() / end_frame()  — Universe path
// -----------------------------------------------------------------

void Starfield::begin_frame(f32 mag_limit)
{
    m_pending_vertices.clear();
    m_pending_vertices.reserve(std::min(m_buffer_capacity, 100000u));
    m_push_constants.mag_limit = mag_limit;
}

void Starfield::add_celestial_object(Vec2f screen_pos,
                                     const universe::CelestialObject& obj)
{
    if (m_pending_vertices.size() >= m_buffer_capacity)
    {
        return;
    }

    m_pending_vertices.push_back(StarVertex{
        .screen_x = screen_pos.x,
        .screen_y = screen_pos.y,
        .mag_v    = obj.mag_v,
        .color_bv = obj.color_bv,
    });
}

void Starfield::end_frame()
{
    m_visible_count = static_cast<u32>(m_pending_vertices.size());

    if (m_visible_count == 0)
    {
        m_push_constants.brightest_mag = kReferenceMag;
        return;
    }

    // Compute brightest magnitude for push constant normalisation.
    f32 min_mag = std::numeric_limits<f32>::max();
    for (const auto& v : m_pending_vertices)
    {
        if (v.mag_v < min_mag) min_mag = v.mag_v;
    }
    m_push_constants.brightest_mag = min_mag;

    upload_star_data(m_pending_vertices);
}

// -----------------------------------------------------------------
// update() — Skychart transform pipeline (NO atmosphere) — LEGACY
//
// Uses Coordinates::project_radec_to_screen() — the SAME shared
// function as constellation overlays.
// -----------------------------------------------------------------

void Starfield::update(std::span<const catalog::StarEntry> stars,
                       std::span<const u32> candidate_indices,
                       const astro::ObserverLocation& observer,
                       f64 lst,
                       const Camera& camera)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const f32 mag_limit = camera.get_magnitude_limit();

    // Diagnostic counters
    u32 diag_pass_mag     = 0;
    u32 diag_pass_project = 0;

    std::vector<StarVertex> vertices;
    vertices.reserve(std::min(static_cast<u32>(candidate_indices.size()), m_buffer_capacity));

    m_visible_indices.clear();
    m_visible_screen_positions.clear();

    f32 min_mag = std::numeric_limits<f32>::max();

    for (const u32 idx : candidate_indices)
    {
        const auto& star = stars[idx];

        // 1. Magnitude limit — the ONLY brightness filter
        if (star.mag_v > mag_limit)
        {
            continue;
        }
        ++diag_pass_mag;

        // 2-4. RA/Dec → screen NDC via SHARED projection function
        const auto screen_pos = astro::Coordinates::project_radec_to_screen(
            star.ra, star.dec, observer, lst, pointing, fov_rad);

        if (!screen_pos.has_value())
        {
            continue;
        }
        ++diag_pass_project;

        // 5. Pack vertex — raw catalog data, no atmospheric modification
        vertices.push_back(StarVertex{
            .screen_x = screen_pos->x,
            .screen_y = screen_pos->y,
            .mag_v    = star.mag_v,
            .color_bv = star.color_bv,  // Real B-V, no reddening
        });

        // Track visible star index and screen position for selection picking
        m_visible_indices.push_back(idx);
        m_visible_screen_positions.push_back(Vec2f{screen_pos->x, screen_pos->y});

        if (star.mag_v < min_mag) min_mag = star.mag_v;

        if (vertices.size() >= m_buffer_capacity)
        {
            break;
        }
    }

    m_visible_count = static_cast<u32>(vertices.size());

    // Update push constants
    m_push_constants.brightest_mag = (m_visible_count > 0) ? min_mag : kReferenceMag;
    m_push_constants.mag_limit = mag_limit;

    if (m_visible_count > 0)
    {
        upload_star_data(vertices);
    }

    // Diagnostic log — once per second (~60 frames)
    static u32 diag_frame_counter = 0;
    if (++diag_frame_counter >= 60)
    {
        diag_frame_counter = 0;
        PLX_CORE_INFO("=== Skychart Pipeline ===");
        PLX_CORE_INFO("  Catalog: {} | Candidates: {} | Mag passed: {} (MLIM {:.1f})",
                      stars.size(), candidate_indices.size(), diag_pass_mag, mag_limit);
        PLX_CORE_INFO("  Projected: {} | GPU: {}",
                      diag_pass_project, m_visible_count);
        PLX_CORE_INFO("  Camera: alt={:.1f} az={:.1f} fov={:.1f}",
                      pointing.alt * astro_constants::kRadToDeg,
                      pointing.az * astro_constants::kRadToDeg,
                      fov_rad * astro_constants::kRadToDeg);
    }
}

// -----------------------------------------------------------------
// draw()
// -----------------------------------------------------------------

void Starfield::draw(VkCommandBuffer cmd) const
{
    if (m_visible_count == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_layout, 0, 1, &m_descriptor_set, 0, nullptr);
    vkCmdPushConstants(cmd, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(StarfieldPushConstants), &m_push_constants);
    vkCmdDraw(cmd, 1, m_visible_count, 0, 0);
}

u32 Starfield::get_visible_count() const { return m_visible_count; }
VkPipeline Starfield::get_pipeline() const { return m_pipeline; }
VkPipelineLayout Starfield::get_pipeline_layout() const { return m_pipeline_layout; }

std::span<const u32> Starfield::get_visible_indices() const { return m_visible_indices; }
std::span<const Vec2f> Starfield::get_screen_positions() const { return m_visible_screen_positions; }

// -----------------------------------------------------------------
// Vulkan resource creation (unchanged)
// -----------------------------------------------------------------

void Starfield::create_storage_buffer(u32 max_stars)
{
    m_buffer_capacity = max_stars;
    const VkDeviceSize buffer_size = sizeof(StarVertex) * max_stars;

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_context.get_device();
    check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &m_storage_buffer),
             "vkCreateBuffer (starfield storage)");

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, m_storage_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context.get_physical_device(), mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_storage_memory),
             "vkAllocateMemory (starfield storage)");
    check_vk(vkBindBufferMemory(device, m_storage_buffer, m_storage_memory, 0),
             "vkBindBufferMemory (starfield storage)");
    check_vk(vkMapMemory(device, m_storage_memory, 0, buffer_size, 0, &m_mapped_ptr),
             "vkMapMemory (starfield storage)");
}

void Starfield::create_descriptor_set_layout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    check_vk(vkCreateDescriptorSetLayout(m_context.get_device(), &layout_info, nullptr,
                                         &m_descriptor_set_layout),
             "vkCreateDescriptorSetLayout (starfield)");
}

void Starfield::create_descriptor_pool_and_set()
{
    VkDevice device = m_context.get_device();

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    check_vk(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptor_pool),
             "vkCreateDescriptorPool (starfield)");

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &m_descriptor_set_layout;

    check_vk(vkAllocateDescriptorSets(device, &alloc_info, &m_descriptor_set),
             "vkAllocateDescriptorSets (starfield)");

    VkDescriptorBufferInfo buffer_desc{};
    buffer_desc.buffer = m_storage_buffer;
    buffer_desc.offset = 0;
    buffer_desc.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptor_set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_desc;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void Starfield::create_pipeline(VkRenderPass render_pass,
                                const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    VkShaderModule vert_module = create_shader_module(shader_dir / "starfield.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "starfield.frag.spv");

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

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
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

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(StarfieldPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_descriptor_set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    check_vk(vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
             "vkCreatePipelineLayout (starfield)");

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
             "vkCreateGraphicsPipelines (starfield)");

    PLX_CORE_INFO("Starfield pipeline created (skychart, additive blend)");

    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

VkShaderModule Starfield::create_shader_module(const std::filesystem::path& path) const
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
             "vkCreateShaderModule (starfield)");
    return module;
}

void Starfield::upload_star_data(const std::vector<StarVertex>& vertices)
{
    std::memcpy(m_mapped_ptr, vertices.data(), vertices.size() * sizeof(StarVertex));
}

} // namespace parallax::rendering