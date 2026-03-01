/// @file font.cpp
/// @brief GPU bitmap font renderer implementation.

#include "ui/font.hpp"

#include "core/logger.hpp"
#include "ui/font_data.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
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

    PLX_CORE_CRITICAL("Failed to find suitable memory type (font)");
    std::abort();
}

} // anonymous namespace

namespace parallax::ui
{

// =================================================================
// Construction / Destruction
// =================================================================

BitmapFont::BitmapFont(const vulkan::Context& context,
                       VkRenderPass render_pass,
                       const std::filesystem::path& shader_dir)
    : m_context{context}
{
    m_batch.reserve(kMaxVertices);

    create_font_atlas();
    create_vertex_buffer();
    create_descriptor_set_layout();
    create_descriptor_pool_and_set();
    create_pipeline(render_pass, shader_dir);

    PLX_CORE_INFO("BitmapFont initialized ({}x{} atlas, {} max chars/frame)",
                  kAtlasW, kAtlasH, kMaxChars);
}

BitmapFont::~BitmapFont()
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
    if (m_vertex_buffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device, m_vertex_buffer, nullptr);
    }
    if (m_vertex_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_vertex_memory, nullptr);
    }
    if (m_atlas_sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(device, m_atlas_sampler, nullptr);
    }
    if (m_atlas_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, m_atlas_view, nullptr);
    }
    if (m_atlas_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, m_atlas_image, nullptr);
    }
    if (m_atlas_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, m_atlas_memory, nullptr);
    }

    PLX_CORE_TRACE("BitmapFont destroyed");
}

// =================================================================
// draw_text() — queue text into the batch
// =================================================================

void BitmapFont::draw_text(const std::string& text, f32 x, f32 y,
                           f32 scale, Vec3f color)
{
    const f32 glyph_w = static_cast<f32>(kFontGlyphW) * scale;
    const f32 glyph_h = static_cast<f32>(kFontGlyphH) * scale;

    const f32 u_step = 1.0f / static_cast<f32>(kAtlasCols);
    const f32 v_step = 1.0f / static_cast<f32>(kAtlasRows);

    f32 cursor_x = x;

    for (const char ch : text)
    {
        int idx = static_cast<int>(ch) - kFontCharFirst;
        if (idx < 0 || idx >= kFontCharCount)
        {
            cursor_x += glyph_w;
            continue;
        }

        const u32 col = static_cast<u32>(idx) % kAtlasCols;
        const u32 row = static_cast<u32>(idx) / kAtlasCols;

        const f32 u0 = static_cast<f32>(col) * u_step;
        const f32 v0 = static_cast<f32>(row) * v_step;
        const f32 u1 = u0 + u_step;
        const f32 v1 = v0 + v_step;

        const f32 x0 = cursor_x;
        const f32 y0 = y;
        const f32 x1 = cursor_x + glyph_w;
        const f32 y1 = y + glyph_h;

        // Triangle 1: top-left, bottom-left, bottom-right
        m_batch.push_back(TextVertex{.position = {x0, y0}, .texcoord = {u0, v0}, .color = color});
        m_batch.push_back(TextVertex{.position = {x0, y1}, .texcoord = {u0, v1}, .color = color});
        m_batch.push_back(TextVertex{.position = {x1, y1}, .texcoord = {u1, v1}, .color = color});

        // Triangle 2: top-left, bottom-right, top-right
        m_batch.push_back(TextVertex{.position = {x0, y0}, .texcoord = {u0, v0}, .color = color});
        m_batch.push_back(TextVertex{.position = {x1, y1}, .texcoord = {u1, v1}, .color = color});
        m_batch.push_back(TextVertex{.position = {x1, y0}, .texcoord = {u1, v0}, .color = color});

        cursor_x += glyph_w;

        if (m_batch.size() >= kMaxVertices)
        {
            break;
        }
    }
}

// =================================================================
// render() — upload batch + draw + clear
// =================================================================

void BitmapFont::render(VkCommandBuffer cmd, VkExtent2D viewport_extent)
{
    if (m_batch.empty())
    {
        return;
    }

    upload_vertices();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipeline_layout, 0, 1,
                            &m_descriptor_set, 0, nullptr);

    TextPushConstants pc{
        .viewport_w = static_cast<f32>(viewport_extent.width),
        .viewport_h = static_cast<f32>(viewport_extent.height),
    };
    vkCmdPushConstants(cmd, m_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(TextPushConstants), &pc);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertex_buffer, &offset);

    vkCmdDraw(cmd, static_cast<u32>(m_batch.size()), 1, 0, 0);

    m_batch.clear();
}

// =================================================================
// Font atlas: rasterize embedded bitmap → VkImage (R8_UNORM)
// =================================================================

void BitmapFont::create_font_atlas()
{
    VkDevice device = m_context.get_device();

    // -----------------------------------------------------------------
    // Build pixel data from the embedded bitmap (R8_UNORM: 0 or 255)
    // -----------------------------------------------------------------
    std::vector<uint8_t> pixels(kAtlasW * kAtlasH, 0);

    for (int i = 0; i < kFontCharCount; ++i)
    {
        const u32 col = static_cast<u32>(i) % kAtlasCols;
        const u32 row = static_cast<u32>(i) / kAtlasCols;

        for (u32 glyph_row = 0; glyph_row < kFontGlyphH; ++glyph_row)
        {
            const uint8_t bits = kFontBitmap[i * kFontGlyphH + glyph_row];

            for (u32 bit = 0; bit < kFontGlyphW; ++bit)
            {
                const bool on = (bits >> (7 - bit)) & 1;
                const u32 px = col * kFontGlyphW + bit;
                const u32 py = row * kFontGlyphH + glyph_row;
                pixels[py * kAtlasW + px] = on ? 255 : 0;
            }
        }
    }

    // -----------------------------------------------------------------
    // Create VkImage
    // -----------------------------------------------------------------
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8_UNORM;
    image_info.extent = {kAtlasW, kAtlasH, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;

    check_vk(vkCreateImage(device, &image_info, nullptr, &m_atlas_image),
             "vkCreateImage (font atlas)");

    // -----------------------------------------------------------------
    // Allocate + bind memory (host-visible for direct write)
    // -----------------------------------------------------------------
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, m_atlas_image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context.get_physical_device(), mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_atlas_memory),
             "vkAllocateMemory (font atlas)");

    check_vk(vkBindImageMemory(device, m_atlas_image, m_atlas_memory, 0),
             "vkBindImageMemory (font atlas)");

    // -----------------------------------------------------------------
    // Copy pixel data (respecting row pitch for linear tiling)
    // -----------------------------------------------------------------
    VkImageSubresource subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.mipLevel = 0;
    subresource.arrayLayer = 0;

    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(device, m_atlas_image, &subresource, &layout);

    void* mapped = nullptr;
    check_vk(vkMapMemory(device, m_atlas_memory, 0, mem_reqs.size, 0, &mapped),
             "vkMapMemory (font atlas)");

    auto* dst = static_cast<uint8_t*>(mapped);
    for (u32 r = 0; r < kAtlasH; ++r)
    {
        std::memcpy(dst + r * layout.rowPitch,
                    pixels.data() + r * kAtlasW,
                    kAtlasW);
    }

    vkUnmapMemory(device, m_atlas_memory);

    // -----------------------------------------------------------------
    // Transition to SHADER_READ_ONLY_OPTIMAL (inline single-time cmd)
    // -----------------------------------------------------------------
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = m_context.get_graphics_queue_family();

    VkCommandPool tmp_pool = VK_NULL_HANDLE;
    check_vk(vkCreateCommandPool(device, &pool_ci, nullptr, &tmp_pool),
             "vkCreateCommandPool (font tmp)");

    VkCommandBufferAllocateInfo alloc_cmd{};
    alloc_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_cmd.commandPool = tmp_pool;
    alloc_cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_cmd.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    check_vk(vkAllocateCommandBuffers(device, &alloc_cmd, &cmd),
             "vkAllocateCommandBuffers (font tmp)");

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check_vk(vkBeginCommandBuffer(cmd, &begin_info),
             "vkBeginCommandBuffer (font tmp)");

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_atlas_image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    check_vk(vkEndCommandBuffer(cmd), "vkEndCommandBuffer (font tmp)");

    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkFenceCreateInfo fence_ci{};
    fence_ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    check_vk(vkCreateFence(device, &fence_ci, nullptr, &fence),
             "vkCreateFence (font tmp)");

    check_vk(vkQueueSubmit(m_context.get_graphics_queue(), 1, &submit_info, fence),
             "vkQueueSubmit (font tmp)");

    check_vk(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX),
             "vkWaitForFences (font tmp)");

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, tmp_pool, nullptr);

    // -----------------------------------------------------------------
    // Image view
    // -----------------------------------------------------------------
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = m_atlas_image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    check_vk(vkCreateImageView(device, &view_info, nullptr, &m_atlas_view),
             "vkCreateImageView (font atlas)");

    // -----------------------------------------------------------------
    // Sampler: nearest filtering for pixel-sharp text
    // -----------------------------------------------------------------
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    check_vk(vkCreateSampler(device, &sampler_info, nullptr, &m_atlas_sampler),
             "vkCreateSampler (font atlas)");

    PLX_CORE_TRACE("Font atlas created: {}x{} R8_UNORM, {} glyphs",
                   kAtlasW, kAtlasH, kFontCharCount);
}

// =================================================================
// Vertex buffer (host-visible, persistently mapped)
// =================================================================

void BitmapFont::create_vertex_buffer()
{
    const VkDeviceSize buffer_size = kMaxVertices * sizeof(TextVertex);

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = buffer_size;
    buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_context.get_device();

    check_vk(vkCreateBuffer(device, &buffer_info, nullptr, &m_vertex_buffer),
             "vkCreateBuffer (text VBO)");

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, m_vertex_buffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        m_context.get_physical_device(), mem_reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    check_vk(vkAllocateMemory(device, &alloc_info, nullptr, &m_vertex_memory),
             "vkAllocateMemory (text VBO)");

    check_vk(vkBindBufferMemory(device, m_vertex_buffer, m_vertex_memory, 0),
             "vkBindBufferMemory (text VBO)");

    check_vk(vkMapMemory(device, m_vertex_memory, 0, buffer_size, 0, &m_vertex_mapped),
             "vkMapMemory (text VBO)");
}

// =================================================================
// upload_vertices() — copy batch to mapped VBO
// =================================================================

void BitmapFont::upload_vertices()
{
    const auto byte_count = m_batch.size() * sizeof(TextVertex);
    std::memcpy(m_vertex_mapped, m_batch.data(), byte_count);
}

// =================================================================
// Descriptor layout: combined image sampler at binding 0
// =================================================================

void BitmapFont::create_descriptor_set_layout()
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;

    check_vk(
        vkCreateDescriptorSetLayout(m_context.get_device(), &layout_info, nullptr,
                                    &m_descriptor_set_layout),
        "vkCreateDescriptorSetLayout (font)");
}

// =================================================================
// Descriptor pool + set: write the font atlas sampler
// =================================================================

void BitmapFont::create_descriptor_pool_and_set()
{
    VkDevice device = m_context.get_device();

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;

    check_vk(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_descriptor_pool),
             "vkCreateDescriptorPool (font)");

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = m_descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &m_descriptor_set_layout;

    check_vk(vkAllocateDescriptorSets(device, &alloc_info, &m_descriptor_set),
             "vkAllocateDescriptorSets (font)");

    VkDescriptorImageInfo image_desc{};
    image_desc.sampler = m_atlas_sampler;
    image_desc.imageView = m_atlas_view;
    image_desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptor_set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &image_desc;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// =================================================================
// Graphics pipeline: triangle list, vertex input, alpha blending
// =================================================================

void BitmapFont::create_pipeline(VkRenderPass render_pass,
                                 const std::filesystem::path& shader_dir)
{
    VkDevice device = m_context.get_device();

    // -----------------------------------------------------------------
    // Shader modules
    // -----------------------------------------------------------------
    VkShaderModule vert_module = create_shader_module(shader_dir / "text.vert.spv");
    VkShaderModule frag_module = create_shader_module(shader_dir / "text.frag.spv");

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
    // Vertex input: position (vec2), texcoord (vec2), color (vec3)
    // -----------------------------------------------------------------
    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof(TextVertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attr_descs{};

    attr_descs[0].binding = 0;
    attr_descs[0].location = 0;
    attr_descs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attr_descs[0].offset = offsetof(TextVertex, position);

    attr_descs[1].binding = 0;
    attr_descs[1].location = 1;
    attr_descs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attr_descs[1].offset = offsetof(TextVertex, texcoord);

    attr_descs[2].binding = 0;
    attr_descs[2].location = 2;
    attr_descs[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attr_descs[2].offset = offsetof(TextVertex, color);

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding_desc;
    vertex_input.vertexAttributeDescriptionCount = static_cast<u32>(attr_descs.size());
    vertex_input.pVertexAttributeDescriptions = attr_descs.data();

    // -----------------------------------------------------------------
    // Input assembly: triangle list
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
    // Rasterizer: fill, no cull
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
    // Alpha blending: src_alpha, one_minus_src_alpha (standard)
    // -----------------------------------------------------------------
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                          | VK_COLOR_COMPONENT_G_BIT
                                          | VK_COLOR_COMPONENT_B_BIT
                                          | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;

    // -----------------------------------------------------------------
    // Pipeline layout: descriptor set + push constants (viewport size)
    // -----------------------------------------------------------------
    VkPushConstantRange push_range{};
    push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_range.offset = 0;
    push_range.size = sizeof(TextPushConstants);

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &m_descriptor_set_layout;
    layout_info.pushConstantRangeCount = 1;
    layout_info.pPushConstantRanges = &push_range;

    check_vk(vkCreatePipelineLayout(device, &layout_info, nullptr, &m_pipeline_layout),
             "vkCreatePipelineLayout (font)");

    // -----------------------------------------------------------------
    // Create pipeline
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
        "vkCreateGraphicsPipelines (font)");

    PLX_CORE_INFO("Text pipeline created (alpha blend, vertex-colored)");

    vkDestroyShaderModule(device, frag_module, nullptr);
    vkDestroyShaderModule(device, vert_module, nullptr);
}

// =================================================================
// SPIR-V loader
// =================================================================

VkShaderModule BitmapFont::create_shader_module(const std::filesystem::path& path) const
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
             "vkCreateShaderModule (font)");

    PLX_CORE_TRACE("Shader module loaded: {}", path.filename().string());
    return module;
}

} // namespace parallax::ui