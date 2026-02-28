/// @file pipeline.cpp
/// @brief Render pass, graphics pipeline, and framebuffer implementation.

#include "vulkan/pipeline.hpp"

#include <cstdlib>
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

} // anonymous namespace

namespace parallax::vulkan
{

Pipeline::Pipeline(const Context& context,
                   const Swapchain& swapchain,
                   const std::filesystem::path& shader_dir)
    : m_context{context}
    , m_extent{swapchain.get_extent()}
{
    create_render_pass(swapchain.get_image_format());
    create_pipeline(shader_dir);
    create_framebuffers(swapchain);
}

Pipeline::~Pipeline()
{
    VkDevice device = m_context.get_device();

    destroy_framebuffers();

    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        PLX_CORE_TRACE("Graphics pipeline destroyed");
    }

    if (m_pipeline_layout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipeline_layout, nullptr);
    }

    if (m_render_pass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, m_render_pass, nullptr);
        PLX_CORE_TRACE("Render pass destroyed");
    }
}

void Pipeline::recreate_framebuffers(const Swapchain& swapchain)
{
    destroy_framebuffers();
    m_extent = swapchain.get_extent();
    create_framebuffers(swapchain);
}

// -----------------------------------------------------------------
// Render pass: single subpass, color attachment, clear to black
// -----------------------------------------------------------------
void Pipeline::create_render_pass(VkFormat color_format)
{
    VkAttachmentDescription color_attachment{};
    color_attachment.format = color_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;

    // Subpass dependency: external → subpass 0 (wait for image acquisition)
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &color_attachment;
    create_info.subpassCount = 1;
    create_info.pSubpasses = &subpass;
    create_info.dependencyCount = 1;
    create_info.pDependencies = &dependency;

    check_vk(
        vkCreateRenderPass(m_context.get_device(), &create_info, nullptr, &m_render_pass),
        "vkCreateRenderPass");

    PLX_CORE_INFO("Render pass created");
}

// -----------------------------------------------------------------
// Graphics pipeline: POINT_LIST, no depth, no blend, dynamic viewport
// -----------------------------------------------------------------
void Pipeline::create_pipeline(const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    // -----------------------------------------------------------------
    // Load shader modules
    // -----------------------------------------------------------------
    VkShaderModule vert_module = create_shader_module(shader_dir / "test_star.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "test_star.frag.spv");

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
    // Vertex input: none (hardcoded in shader)
    // -----------------------------------------------------------------
    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 0;
    vertex_input.vertexAttributeDescriptionCount = 0;

    // -----------------------------------------------------------------
    // Input assembly: POINT_LIST
    // -----------------------------------------------------------------
    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    // -----------------------------------------------------------------
    // Dynamic viewport and scissor
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
    // Actual viewport/scissor set dynamically at draw time

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
    // Color blending: no blending (overwrite)
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
    // Pipeline layout: empty (no descriptors, no push constants)
    // -----------------------------------------------------------------
    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    check_vk(
        vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
        "vkCreatePipelineLayout");

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
    pipeline_info.pDepthStencilState = nullptr; // No depth
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = m_pipeline_layout;
    pipeline_info.renderPass = m_render_pass;
    pipeline_info.subpass = 0;

    check_vk(
        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline),
        "vkCreateGraphicsPipelines");

    PLX_CORE_INFO("Graphics pipeline created (POINT_LIST, dynamic viewport/scissor)");

    // -----------------------------------------------------------------
    // Destroy shader modules �� no longer needed after pipeline creation
    // -----------------------------------------------------------------
    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

// -----------------------------------------------------------------
// Framebuffers: one per swapchain image view
// -----------------------------------------------------------------
void Pipeline::create_framebuffers(const Swapchain& swapchain)
{
    const auto& image_views = swapchain.get_image_views();
    VkExtent2D extent = swapchain.get_extent();
    m_framebuffers.resize(image_views.size());

    for (std::size_t i = 0; i < image_views.size(); ++i)
    {
        VkImageView attachments[] = {image_views[i]};

        VkFramebufferCreateInfo fb_info{};
        fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb_info.renderPass = m_render_pass;
        fb_info.attachmentCount = 1;
        fb_info.pAttachments = attachments;
        fb_info.width = extent.width;
        fb_info.height = extent.height;
        fb_info.layers = 1;

        check_vk(
            vkCreateFramebuffer(m_context.get_device(), &fb_info, nullptr, &m_framebuffers[i]),
            "vkCreateFramebuffer");
    }

    PLX_CORE_INFO("Framebuffers created: {} ({}x{})",
                  m_framebuffers.size(), extent.width, extent.height);
}

void Pipeline::destroy_framebuffers()
{
    for (auto fb : m_framebuffers)
    {
        if (fb != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(m_context.get_device(), fb, nullptr);
        }
    }
    m_framebuffers.clear();
}

// -----------------------------------------------------------------
// SPIR-V file loader → VkShaderModule
// -----------------------------------------------------------------
VkShaderModule Pipeline::create_shader_module(const std::filesystem::path& path) const
{
    // Read entire file as binary
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
    check_vk(
        vkCreateShaderModule(m_context.get_device(), &create_info, nullptr, &module),
        "vkCreateShaderModule");

    PLX_CORE_TRACE("Shader module loaded: {}", path.filename().string());
    return module;
}

// -----------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------

VkRenderPass Pipeline::get_render_pass() const
{
    return m_render_pass;
}

VkPipeline Pipeline::get_pipeline() const
{
    return m_pipeline;
}

VkPipelineLayout Pipeline::get_layout() const
{
    return m_pipeline_layout;
}

VkFramebuffer Pipeline::get_framebuffer(uint32_t image_index) const
{
    return m_framebuffers[image_index];
}

} // namespace parallax::vulkan