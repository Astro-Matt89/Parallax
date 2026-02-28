/// @file starfield.cpp
/// @brief Starfield renderer implementation: CPU transforms + GPU buffer + pipeline.

#include "rendering/starfield.hpp"

#include "core/logger.hpp"
#include "core/types.hpp"

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

/// @brief Find a suitable memory type for the given requirements.
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

    PLX_CORE_INFO("Starfield renderer initialized (buffer capacity: {} stars)", max_stars);
}

Starfield::~Starfield()
{
    VkDevice device = m_context.get_device();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
    }
    if (m_pipeline_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
    }
    if (m_descriptor_pool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(device, m_descriptor_pool, nullptr);
    }
    if (m_descriptor_set_layout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(device, m_descriptor_set_layout, nullptr);
    }
    if (m_storage_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, m_storage_buffer, nullptr);
    }
    if (m_storage_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_storage_memory, nullptr);
    }

    PLX_CORE_TRACE("Starfield renderer destroyed");
}

// -----------------------------------------------------------------
// update() — CPU-side transform pipeline
// -----------------------------------------------------------------

void Starfield::update(std::span<const catalog::StarEntry> stars,
                       const astro::ObserverLocation& observer,
                       f64 lst,
                       const Camera& camera)
{
    const auto pointing = camera.get_pointing();
    const f64 fov_rad = camera.get_fov_rad();
    const f32 mag_limit = camera.get_magnitude_limit();

    std::vector<StarVertex> vertices;
    vertices.reserve(std::min(static_cast<u32>(stars.size()), m_buffer_capacity));

    for (const auto& star : stars)
    {
        // Skip stars fainter than the magnitude limit
        if (star.mag_v > mag_limit)
        {
            continue;
        }

        // RA/Dec → Alt/Az
        const astro::EquatorialCoord eq{.ra = star.ra, .dec = star.dec};
        const auto hz = astro::Coordinates::equatorial_to_horizontal(eq, observer, lst);

        // Skip stars below the horizon
        if (hz.alt < 0.0)
        {
            continue;
        }

        // Alt/Az → screen projection
        const auto screen_pos = astro::Coordinates::horizontal_to_screen(hz, pointing, fov_rad);
        if (!screen_pos.has_value())
        {
            continue;
        }

        // Magnitude → brightness (Pogson formula)
        // brightness = 10^(-0.4 * (mag - mag_zero))
        //
        // Normalize so mag=0 → brightness=1.0 (Vega system).
        // Brighter stars (negative mag) get values > 1.0,
        // fainter stars get values < 1.0.
        // We normalize in the shader via the brightness_scale push constant.
        const f64 raw_brightness = std::pow(10.0, -0.4 * (static_cast<f64>(star.mag_v) - kMagZero));

        // Normalize to [0, 1] range using a reference:
        // Sirius at mag -1.46 gives ~3.84, we want that to map to ~1.0
        // Use a simple normalization: brightness / max_expected_brightness
        // max_expected is for mag = -1.5 → pow(10, 0.6) ≈ 3.98
        constexpr f64 kMaxBrightness = 3.98;
        const f64 brightness = std::min(raw_brightness / kMaxBrightness, 1.0);

        vertices.push_back(StarVertex{
            .screen_x   = screen_pos->x,
            .screen_y   = screen_pos->y,
            .brightness = static_cast<f32>(brightness),
            .color_bv   = star.color_bv,
        });

        // Don't exceed buffer capacity
        if (vertices.size() >= m_buffer_capacity)
        {
            break;
        }
    }

    m_visible_count = static_cast<u32>(vertices.size());

    if (m_visible_count > 0)
    {
        upload_star_data(vertices);
    }
}

// -----------------------------------------------------------------
// draw() — record draw commands
// -----------------------------------------------------------------

void Starfield::draw(VkCommandBuffer cmd) const
{
    if (m_visible_count == 0)
    {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_layout, 0, 1,
                            &m_descriptor_set, 0, nullptr);

    vkCmdPushConstants(cmd, m_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(StarfieldPushConstants),
                       &m_push_constants);

    // Instanced draw: 1 vertex per instance, m_visible_count instances
    vkCmdDraw(cmd, 1, m_visible_count, 0, 0);
}

// -----------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------

u32 Starfield::get_visible_count() const
{
    return m_visible_count;
}

VkPipeline Starfield::get_pipeline() const
{
    return m_pipeline;
}

VkPipelineLayout Starfield::get_pipeline_layout() const
{
    return m_pipeline_layout;
}

// -----------------------------------------------------------------
// Storage buffer (host-visible, persistently mapped)
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
        m_context.get_physical_device(),
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_storage_memory),
             "vkAllocateMemory (starfield storage)");

    check_vk(vkBindBufferMemory(device, m_storage_buffer, m_storage_memory, 0),
             "vkBindBufferMemory (starfield storage)");

    // Persistently map the buffer
    check_vk(vkMapMemory(device, m_storage_memory, 0, buffer_size, 0, &m_mapped_ptr),
             "vkMapMemory (starfield storage)");

    PLX_CORE_TRACE("Starfield storage buffer created: {} bytes ({} stars)",
                   buffer_size, max_stars);
}

// -----------------------------------------------------------------
// Descriptor set layout: single storage buffer at binding 0
// -----------------------------------------------------------------

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

    check_vk(
        vkCreateDescriptorSetLayout(m_context.get_device(), &layout_info, nullptr,
                                    &m_descriptor_set_layout),
        "vkCreateDescriptorSetLayout (starfield)");
}

// -----------------------------------------------------------------
// Descriptor pool + set
// -----------------------------------------------------------------

void Starfield::create_descriptor_pool_and_set()
{
    VkDevice device = m_context.get_device();

    // Pool
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

    // Allocate set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &m_descriptor_set_layout;

    check_vk(vkAllocateDescriptorSets(device, &alloc_info, &m_descriptor_set),
             "vkAllocateDescriptorSets (starfield)");

    // Write the storage buffer into the descriptor set
    VkDescriptorBufferInfo buffer_desc{};
    buffer_desc.buffer = m_storage_buffer;
    buffer_desc.offset = 0;
    buffer_desc.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_desc;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// -----------------------------------------------------------------
// Graphics pipeline: starfield shaders, additive blending, push constants
// -----------------------------------------------------------------

void Starfield::create_pipeline(VkRenderPass render_pass,
                                const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    // Shader modules
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

    // No vertex input (data from storage buffer)
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // POINT_LIST topology
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic viewport + scissor
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling: off
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // -----------------------------------------------------------------
    // Additive blending: src.rgb * srcAlpha + dst.rgb * 1
    // Stars accumulate light on top of the dark background.
    // -----------------------------------------------------------------
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                          | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT
                                          | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    // -----------------------------------------------------------------
    // Pipeline layout: descriptor set (storage buffer) + push constants
    // -----------------------------------------------------------------
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

    // -----------------------------------------------------------------
    // Create the graphics pipeline
    // -----------------------------------------------------------------
    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pDepthStencilState = nullptr;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    check_vk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline),
        "vkCreateGraphicsPipelines (starfield)");

    PLX_CORE_INFO("Starfield pipeline created (POINT_LIST, additive blend, storage buffer)");

    // Clean up shader modules
    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

// -----------------------------------------------------------------
// Shader module loader
// -----------------------------------------------------------------

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
        PLX_CORE_CRITICAL("Invalid SPIR-V file (size {} not aligned to 4): {}",
                          file_size, path.string());
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

    PLX_CORE_TRACE("Shader module loaded: {}", path.filename().string());
    return module;
}

// -----------------------------------------------------------------
// Upload star data to persistently mapped buffer
// -----------------------------------------------------------------

void Starfield::upload_star_data(const std::vector<StarVertex>& vertices)
{
    const std::size_t byte_size = vertices.size() * sizeof(StarVertex);
    std::memcpy(m_mapped_ptr, vertices.data(), byte_size);
}

} // namespace parallax::rendering