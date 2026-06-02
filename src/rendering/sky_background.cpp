/// @file sky_background.cpp
/// @brief Procedural sky gradient renderer implementation.

#include "rendering/sky_background.hpp"

#include "core/logger.hpp"
#include "ui/shell/viewport_rect.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

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
        if ((type_filter & (1u << i))
            && (mem_props.memoryTypes[i].propertyFlags & properties) == properties)
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

// =================================================================
// Construction / Destruction
// =================================================================

SkyBackground::SkyBackground(const vulkan::Context& context,
                             VkRenderPass render_pass,
                             const std::filesystem::path& shader_dir,
                             VkExtent2D extent)
    : m_context{context}
    , m_extent{extent}
{
    create_uniform_buffer();
    create_descriptor_set_layout();
    create_descriptor_pool_and_set();
    create_pipeline(render_pass, shader_dir);

    PLX_CORE_INFO("Sky background renderer initialized");
}

SkyBackground::~SkyBackground()
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
    if (m_uniform_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, m_uniform_buffer, nullptr);
    }
    if (m_uniform_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_uniform_memory, nullptr);
    }

    PLX_CORE_TRACE("Sky background renderer destroyed");
}

// =================================================================
// update_params() — cache sky state for this frame
// =================================================================

void SkyBackground::update_params(const SkyParams& params, const Camera& camera, f32 aspect_ratio)
{
    const auto pointing = camera.get_pointing();

    m_uniform_data = SkyUniformData{
        .camera_alt_rad    = static_cast<f32>(pointing.alt),
        .camera_az_rad     = static_cast<f32>(pointing.az),
        .fov_rad           = static_cast<f32>(camera.get_fov_rad()),
        .aspect_ratio      = aspect_ratio,
        .bortle_scale      = params.bortle_scale,
        .sun_altitude_deg  = params.sun_altitude_deg,
        .sun_azimuth_deg   = params.sun_azimuth_deg,
        .moon_altitude_deg = params.moon_altitude_deg,
        .moon_azimuth_deg  = params.moon_azimuth_deg,
        .moon_illumination = params.moon_illumination,
        .atmosphere_enabled = params.atmosphere_enabled ? 1u : 0u,
        ._pad0             = 0.0f,
    };

    upload_uniforms();
}

// =================================================================
// draw() — record fullscreen triangle draw
// =================================================================

void SkyBackground::draw(VkCommandBuffer cmd, const ui::shell::ViewportRect& viewport) const
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_layout, 0, 1,
                            &m_descriptor_set, 0, nullptr);

    const SkyViewportPushConstants push_constants{
        .viewport_origin = {
            static_cast<f32>(viewport.x),
            static_cast<f32>(viewport.y)
        },
        .viewport_size = {
            static_cast<f32>(std::max(viewport.width, 1u)),
            static_cast<f32>(std::max(viewport.height, 1u))
        }
    };
    vkCmdPushConstants(cmd,
                       m_pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(SkyViewportPushConstants),
                       &push_constants);

    // Fullscreen triangle: 3 vertices, 1 instance, no vertex buffer
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

// =================================================================
// set_extent() — update viewport dimensions
// =================================================================

void SkyBackground::set_extent(VkExtent2D extent)
{
    m_extent = extent;
}

// =================================================================
// Uniform buffer (host-visible, persistently mapped)
// =================================================================

void SkyBackground::create_uniform_buffer()
{
    const VkDeviceSize buffer_size = sizeof(SkyUniformData);

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_context.get_device();

    check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &m_uniform_buffer),
             "vkCreateBuffer (sky UBO)");

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(device, m_uniform_buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context.get_physical_device(),
        mem_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_uniform_memory),
             "vkAllocateMemory (sky UBO)");

    check_vk(vkBindBufferMemory(device, m_uniform_buffer, m_uniform_memory, 0),
             "vkBindBufferMemory (sky UBO)");

    // Persistently map
    check_vk(vkMapMemory(device, m_uniform_memory, 0, buffer_size, 0, &m_mapped_ptr),
             "vkMapMemory (sky UBO)");

    PLX_CORE_TRACE("Sky background UBO created: {} bytes", buffer_size);
}

// =================================================================
// upload_uniforms() — write uniform data to mapped buffer
// =================================================================

void SkyBackground::upload_uniforms()
{
    std::memcpy(m_mapped_ptr, &m_uniform_data, sizeof(SkyUniformData));
}

// =================================================================
// Descriptor set layout: single UBO at binding 0 (fragment stage)
// =================================================================

void SkyBackground::create_descriptor_set_layout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    check_vk(
        vkCreateDescriptorSetLayout(m_context.get_device(), &layout_info, nullptr,
                                    &m_descriptor_set_layout),
        "vkCreateDescriptorSetLayout (sky)");
}

// =================================================================
// Descriptor pool + set
// =================================================================

void SkyBackground::create_descriptor_pool_and_set()
{
    VkDevice device = m_context.get_device();

    // Pool
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    check_vk(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptor_pool),
             "vkCreateDescriptorPool (sky)");

    // Allocate set
    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &m_descriptor_set_layout;

    check_vk(vkAllocateDescriptorSets(device, &alloc_info, &m_descriptor_set),
             "vkAllocateDescriptorSets (sky)");

    // Write the UBO into the descriptor set
    VkDescriptorBufferInfo buffer_desc{};
    buffer_desc.buffer = m_uniform_buffer;
    buffer_desc.offset = 0;
    buffer_desc.range = sizeof(SkyUniformData);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_desc;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// =================================================================
// Graphics pipeline: fullscreen triangle, no blending, UBO
// =================================================================

void SkyBackground::create_pipeline(VkRenderPass render_pass,
                                    const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    // -----------------------------------------------------------------
    // Shader modules
    // -----------------------------------------------------------------
    VkShaderModule vert_module = create_shader_module(shader_dir / "sky_background.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "sky_background.frag.spv");

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

    // -----------------------------------------------------------------
    // No vertex input (fullscreen triangle generated procedurally)
    // -----------------------------------------------------------------
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    // -----------------------------------------------------------------
    // TRIANGLE_LIST topology (one fullscreen triangle)
    // -----------------------------------------------------------------
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // -----------------------------------------------------------------
    // Dynamic viewport + scissor
    // -----------------------------------------------------------------
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    // -----------------------------------------------------------------
    // Rasterizer
    // -----------------------------------------------------------------
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // -----------------------------------------------------------------
    // Multisampling: off
    // -----------------------------------------------------------------
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // -----------------------------------------------------------------
    // Color blending: no blending (sky overwrites clear color)
    // -----------------------------------------------------------------
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                          | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT
                                          | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    // -----------------------------------------------------------------
    // Pipeline layout: descriptor set (UBO) + viewport push constants
    // -----------------------------------------------------------------
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(SkyViewportPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_descriptor_set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    check_vk(vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
             "vkCreatePipelineLayout (sky)");

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
        "vkCreateGraphicsPipelines (sky)");

    PLX_CORE_INFO("Sky background pipeline created (fullscreen triangle, UBO)");

    // -----------------------------------------------------------------
    // Destroy shader modules — no longer needed after pipeline creation
    // -----------------------------------------------------------------
    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

// =================================================================
// SPIR-V loader
// =================================================================

VkShaderModule SkyBackground::create_shader_module(const std::filesystem::path& path) const
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
             "vkCreateShaderModule (sky)");

    PLX_CORE_TRACE("Shader module loaded: {}", path.filename().string());
    return module;
}

} // namespace parallax::rendering