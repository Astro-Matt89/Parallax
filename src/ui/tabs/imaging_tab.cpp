#include "ui/tabs/imaging_tab.hpp"

#include "core/logger.hpp"
#include "core/user_data_path.hpp"
#include "imaging/image_formation.hpp"
#include "imaging/multispectral_image.hpp"
#include "instruments/array_instrument.hpp"
#include "observation/observation_session.hpp"
#include "observation/session_scheduler.hpp"
#include "observation/session_types.hpp"
#include "rendering/line_renderer.hpp"
#include "ui/font.hpp"
#include "ui/font_data.hpp"
#include "ui/selection.hpp"
#include "ui/tabs/tab_render_helpers.hpp"
#include "ui/widgets.hpp"
#include "universe/celestial_object.hpp"
#include "universe/universe.hpp"
#include "vulkan/context.hpp"
#include "vulkan/swapchain.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace parallax::ui::tabs
{
    namespace
    {
        constexpr Vec4f kBackgroundColor{0.02f, 0.06f, 0.02f, 1.0f};
        constexpr Vec4f kPanelBackground{0.0f, 0.12f, 0.0f, 0.45f};
        constexpr Vec4f kPanelBorder = widget_colors::kBorder;
        constexpr Vec4f kPanelBorderBright = widget_colors::kBorderBright;
        constexpr Vec4f kMarkerColor{0.95f, 0.95f, 0.1f, 1.0f};
        constexpr Vec4f kObjectColor{0.5f, 0.9f, 0.5f, 0.9f};
        constexpr Vec4f kProgressFill{0.0f, 0.75f, 0.0f, 0.8f};
        constexpr Vec3f kTextBright = widget_colors::kTextBright;
        constexpr Vec3f kTextDim = widget_colors::kTextDim;
        constexpr Vec3f kTextDisabled{0.2f, 0.35f, 0.2f};

        constexpr u32 kOuterPadding = 8;
        constexpr u32 kColumnGap = 8;
        constexpr u32 kSectionGap = 8;
        constexpr u32 kControlRowHeight = 20;
        constexpr u32 kButtonHeight = 22;
        constexpr u32 kPreviewHeight = 190;

        constexpr u64 kImageSeedSalt = 0x9E3779B97F4A7C15ULL;

        struct TextureVertex
        {
            Vec2f position_px;
            Vec2f uv;
        };

        [[nodiscard]] Vec2f pixel_to_ndc(const Vec2f px, const Vec2f viewport)
        {
            return {
                (px.x / viewport.x) * 2.0f - 1.0f,
                (px.y / viewport.y) * 2.0f - 1.0f,
            };
        }

        [[nodiscard]] shell::ViewportRect make_rect(i32 x, i32 y, i32 width, i32 height)
        {
            return {
                static_cast<u32>(std::max(0, x)),
                static_cast<u32>(std::max(0, y)),
                static_cast<u32>(std::max(0, width)),
                static_cast<u32>(std::max(0, height)),
            };
        }

        void check_vk(VkResult result, const char* operation)
        {
            if (result != VK_SUCCESS)
            {
                PLX_CORE_CRITICAL("Vulkan error in {}: {}", operation, static_cast<i32>(result));
                std::abort();
            }
        }

        [[nodiscard]] u32 find_memory_type(VkPhysicalDevice physical_device,
                                           u32 type_filter,
                                           VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties mem_props{};
            vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

            for (u32 index = 0; index < mem_props.memoryTypeCount; ++index)
            {
                const bool type_matches = (type_filter & (1u << index)) != 0u;
                const bool properties_match =
                    (mem_props.memoryTypes[index].propertyFlags & properties) == properties;
                if (type_matches && properties_match)
                {
                    return index;
                }
            }

            PLX_CORE_CRITICAL("Failed to find Vulkan memory type for imaging tab");
            std::abort();
        }

        [[nodiscard]] std::vector<u32> read_spirv_words(const std::filesystem::path& shader_path)
        {
            std::ifstream file(shader_path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                PLX_CORE_CRITICAL("Failed to open shader file: {}", shader_path.string());
                std::abort();
            }

            const std::size_t byte_size = static_cast<std::size_t>(file.tellg());
            if (byte_size == 0 || (byte_size % sizeof(u32)) != 0)
            {
                PLX_CORE_CRITICAL("Invalid SPIR-V size {} in {}", byte_size, shader_path.string());
                std::abort();
            }

            std::vector<u32> code(byte_size / sizeof(u32));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(byte_size));
            return code;
        }

        [[nodiscard]] VkShaderModule create_shader_module(VkDevice device, const std::filesystem::path& shader_path)
        {
            const std::vector<u32> code = read_spirv_words(shader_path);

            VkShaderModuleCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            create_info.codeSize = code.size() * sizeof(u32);
            create_info.pCode = code.data();

            VkShaderModule shader_module = VK_NULL_HANDLE;
            check_vk(vkCreateShaderModule(device, &create_info, nullptr, &shader_module),
                     "vkCreateShaderModule (imaging tab)");
            return shader_module;
        }

        void apply_viewport(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            VkViewport viewport{};
            viewport.x = static_cast<f32>(rect.x);
            viewport.y = static_cast<f32>(rect.y);
            viewport.width = static_cast<f32>(rect.width);
            viewport.height = static_cast<f32>(rect.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            scissor.extent = {rect.width, rect.height};
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

        void clear_background(VkCommandBuffer cmd, const shell::ViewportRect& rect)
        {
            VkClearAttachment clear_attachment{};
            clear_attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            clear_attachment.colorAttachment = 0;
            clear_attachment.clearValue.color = {{
                kBackgroundColor.r,
                kBackgroundColor.g,
                kBackgroundColor.b,
                kBackgroundColor.a,
            }};

            VkClearRect clear_rect{};
            clear_rect.rect.offset = {static_cast<i32>(rect.x), static_cast<i32>(rect.y)};
            clear_rect.rect.extent = {rect.width, rect.height};
            clear_rect.baseArrayLayer = 0;
            clear_rect.layerCount = 1;

            vkCmdClearAttachments(cmd, 1, &clear_attachment, 1, &clear_rect);
        }

        [[nodiscard]] std::string format_ra_dec_text(f64 ra_rad, f64 dec_rad)
        {
            const f64 ra_hours = std::fmod((ra_rad * astro_constants::kRadToDeg) / 15.0 + 24.0, 24.0);
            const i32 ra_h = static_cast<i32>(ra_hours);
            const i32 ra_m = static_cast<i32>((ra_hours - static_cast<f64>(ra_h)) * 60.0);

            const f64 dec_deg = dec_rad * astro_constants::kRadToDeg;
            const char sign = dec_deg >= 0.0 ? '+' : '-';
            const f64 abs_dec = std::abs(dec_deg);
            const i32 dec_d = static_cast<i32>(abs_dec);
            const i32 dec_m = static_cast<i32>((abs_dec - static_cast<f64>(dec_d)) * 60.0);

            return std::format("RA {:02d}h{:02d}m  Dec {}{:02d}°{:02d}'", ra_h, ra_m, sign, dec_d, dec_m);
        }

        [[nodiscard]] bool contains_local(const shell::ViewportRect& rect, const Vec2f point)
        {
            return point.x >= static_cast<f32>(rect.x)
                && point.x < static_cast<f32>(rect.right())
                && point.y >= static_cast<f32>(rect.y)
                && point.y < static_cast<f32>(rect.bottom());
        }

        void draw_rect_outline(rendering::LineRenderer& line_renderer,
                               const shell::ViewportRect& rect,
                               const Vec4f color,
                               const VkExtent2D extent)
        {
            if (!rect.is_valid() || extent.width == 0 || extent.height == 0)
            {
                return;
            }

            const Vec2f window{
                static_cast<f32>(extent.width),
                static_cast<f32>(extent.height),
            };

            const f32 x0 = static_cast<f32>(rect.x);
            const f32 y0 = static_cast<f32>(rect.y);
            const f32 x1 = static_cast<f32>(rect.right());
            const f32 y1 = static_cast<f32>(rect.bottom());

            line_renderer.add_line(pixel_to_ndc({x0, y0}, window), pixel_to_ndc({x1, y0}, window), color);
            line_renderer.add_line(pixel_to_ndc({x1, y0}, window), pixel_to_ndc({x1, y1}, window), color);
            line_renderer.add_line(pixel_to_ndc({x1, y1}, window), pixel_to_ndc({x0, y1}, window), color);
            line_renderer.add_line(pixel_to_ndc({x0, y1}, window), pixel_to_ndc({x0, y0}, window), color);
        }

        void draw_filled_rect(rendering::LineRenderer& line_renderer,
                              const shell::ViewportRect& rect,
                              const Vec4f color,
                              const VkExtent2D extent)
        {
            if (!rect.is_valid() || extent.width == 0 || extent.height == 0)
            {
                return;
            }

            const Vec2f window{
                static_cast<f32>(extent.width),
                static_cast<f32>(extent.height),
            };

            const f32 x0 = static_cast<f32>(rect.x);
            const f32 x1 = static_cast<f32>(rect.right());
            for (u32 py = rect.y; py < rect.bottom(); ++py)
            {
                const f32 y = static_cast<f32>(py) + 0.5f;
                line_renderer.add_line(pixel_to_ndc({x0, y}, window), pixel_to_ndc({x1, y}, window), color);
            }
        }

        [[nodiscard]] std::pair<f64, f64> project_gnomonic(f64 ra_obj,
                                                           f64 dec_obj,
                                                           f64 ra_center,
                                                           f64 dec_center,
                                                           f64 pixel_scale_arcsec,
                                                           u32 width,
                                                           u32 height)
        {
            if (width == 0 || height == 0 || pixel_scale_arcsec <= std::numeric_limits<f64>::epsilon())
            {
                return {-1.0e9, -1.0e9};
            }

            const f64 delta_ra = ra_obj - ra_center;
            const f64 sin_dec_center = std::sin(dec_center);
            const f64 cos_dec_center = std::cos(dec_center);
            const f64 sin_dec_obj = std::sin(dec_obj);
            const f64 cos_dec_obj = std::cos(dec_obj);
            const f64 cos_delta_ra = std::cos(delta_ra);

            const f64 cos_c = sin_dec_center * sin_dec_obj + cos_dec_center * cos_dec_obj * cos_delta_ra;
            if (cos_c <= 0.0)
            {
                return {-1.0e9, -1.0e9};
            }

            const f64 x_rad = cos_dec_obj * std::sin(delta_ra) / cos_c;
            const f64 y_rad = (cos_dec_center * sin_dec_obj - sin_dec_center * cos_dec_obj * cos_delta_ra) / cos_c;

            const f64 arcsec_per_rad = 1.0 / astro_constants::kArcSecToRad;
            const f64 x_arcsec = x_rad * arcsec_per_rad;
            const f64 y_arcsec = y_rad * arcsec_per_rad;

            const f64 cx = (static_cast<f64>(width) - 1.0) * 0.5;
            const f64 cy = (static_cast<f64>(height) - 1.0) * 0.5;

            return {
                cx + x_arcsec / pixel_scale_arcsec,
                cy - y_arcsec / pixel_scale_arcsec,
            };
        }
    }

    struct ImagingTab::UiLayout
    {
        shell::ViewportRect root{};
        shell::ViewportRect left_column{};
        shell::ViewportRect right_column{};

        shell::ViewportRect live_preview_rect{};
        shell::ViewportRect integrated_image_rect{};

        shell::ViewportRect band_cycle_button{};
        shell::ViewportRect stretch_cycle_button{};

        shell::ViewportRect change_target_button{};

        std::vector<shell::ViewportRect> station_rows{};
        std::vector<shell::ViewportRect> band_rows{};

        shell::ViewportRect start_button{};
        shell::ViewportRect pause_button{};
        shell::ViewportRect stop_button{};

        shell::ViewportRect save_fits_button{};
        shell::ViewportRect save_png_button{};

        shell::ViewportRect progress_bar{};
    };

    class ImagingTab::UniverseObjectSource final : public imaging::IObjectSource
    {
    public:
        explicit UniverseObjectSource(const universe::Universe& universe)
            : m_universe(universe)
        {
        }

        void query_fov(const double ra_rad,
                       const double dec_rad,
                       const double radius_deg,
                       const float mag_limit,
                       std::vector<universe::CelestialObject>& results) const override
        {
            m_universe.query_fov(ra_rad,
                                 dec_rad,
                                 radius_deg,
                                 mag_limit,
                                 universe::QueryFlags::All,
                                 results);
        }

    private:
        const universe::Universe& m_universe;
    };

    ImagingTab::ImagingTab(const vulkan::Context& context,
                           vulkan::Swapchain& swapchain,
                           const VkRenderPass render_pass,
                           const std::filesystem::path& shader_dir,
                           universe::Universe& universe,
                           instruments::ArrayInstrument& instrument,
                           observation::SessionScheduler& scheduler,
                           Selection& selection,
                           BitmapFont& font,
                           f64& julian_date)
        : m_context(context)
        , m_swapchain(swapchain)
        , m_universe(universe)
        , m_instrument(instrument)
        , m_scheduler(scheduler)
        , m_selection(selection)
        , m_font(font)
        , m_julian_date(julian_date)
    {
        m_line_renderer = std::make_unique<rendering::LineRenderer>(
            m_context,
            render_pass,
            shader_dir);
        m_object_source = std::make_unique<UniverseObjectSource>(m_universe);

        const VkDevice device = m_context.get_device();

        VkDescriptorSetLayoutBinding texture_binding{};
        texture_binding.binding = 0;
        texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texture_binding.descriptorCount = 1;
        texture_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo descriptor_layout_info{};
        descriptor_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptor_layout_info.bindingCount = 1;
        descriptor_layout_info.pBindings = &texture_binding;
        check_vk(vkCreateDescriptorSetLayout(device,
                                             &descriptor_layout_info,
                                             nullptr,
                                             &m_texture_descriptor_set_layout),
                 "vkCreateDescriptorSetLayout (imaging texture)");

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1;

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = 1;
        check_vk(vkCreateDescriptorPool(device, &pool_info, nullptr, &m_texture_descriptor_pool),
                 "vkCreateDescriptorPool (imaging texture)");

        VkDescriptorSetAllocateInfo descriptor_allocate_info{};
        descriptor_allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptor_allocate_info.descriptorPool = m_texture_descriptor_pool;
        descriptor_allocate_info.descriptorSetCount = 1;
        descriptor_allocate_info.pSetLayouts = &m_texture_descriptor_set_layout;
        check_vk(vkAllocateDescriptorSets(device,
                                          &descriptor_allocate_info,
                                          &m_texture_descriptor_set),
                 "vkAllocateDescriptorSets (imaging texture)");

        VkPushConstantRange push_constant_range{};
        push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0;
        push_constant_range.size = sizeof(TextPushConstants);

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = &m_texture_descriptor_set_layout;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pPushConstantRanges = &push_constant_range;
        check_vk(vkCreatePipelineLayout(device,
                                        &pipeline_layout_info,
                                        nullptr,
                                        &m_texture_pipeline_layout),
                 "vkCreatePipelineLayout (imaging texture)");

        const VkShaderModule vertex_shader = create_shader_module(device, shader_dir / "imaging_preview.vert.spv");
        const VkShaderModule fragment_shader = create_shader_module(device, shader_dir / "imaging_preview.frag.spv");

        VkPipelineShaderStageCreateInfo vertex_stage{};
        vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertex_stage.module = vertex_shader;
        vertex_stage.pName = "main";

        VkPipelineShaderStageCreateInfo fragment_stage{};
        fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_stage.module = fragment_shader;
        fragment_stage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> shader_stages{vertex_stage, fragment_stage};

        VkVertexInputBindingDescription binding_description{};
        binding_description.binding = 0;
        binding_description.stride = sizeof(TextureVertex);
        binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions{};
        attribute_descriptions[0].binding = 0;
        attribute_descriptions[0].location = 0;
        attribute_descriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[0].offset = offsetof(TextureVertex, position_px);

        attribute_descriptions[1].binding = 0;
        attribute_descriptions[1].location = 1;
        attribute_descriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attribute_descriptions[1].offset = offsetof(TextureVertex, uv);

        VkPipelineVertexInputStateCreateInfo vertex_input_state{};
        vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex_input_state.vertexBindingDescriptionCount = 1;
        vertex_input_state.pVertexBindingDescriptions = &binding_description;
        vertex_input_state.vertexAttributeDescriptionCount = static_cast<u32>(attribute_descriptions.size());
        vertex_input_state.pVertexAttributeDescriptions = attribute_descriptions.data();

        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterization{};
        rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterization.polygonMode = VK_POLYGON_MODE_FILL;
        rasterization.lineWidth = 1.0f;
        rasterization.cullMode = VK_CULL_MODE_NONE;
        rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                                        | VK_COLOR_COMPONENT_G_BIT
                                        | VK_COLOR_COMPONENT_B_BIT
                                        | VK_COLOR_COMPONENT_A_BIT;
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo color_blend{};
        color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blend.attachmentCount = 1;
        color_blend.pAttachments = &blend_attachment;

        std::array<VkDynamicState, 2> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<u32>(dynamic_states.size());
        dynamic_state.pDynamicStates = dynamic_states.data();

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = static_cast<u32>(shader_stages.size());
        pipeline_info.pStages = shader_stages.data();
        pipeline_info.pVertexInputState = &vertex_input_state;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterization;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pColorBlendState = &color_blend;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = m_texture_pipeline_layout;
        pipeline_info.renderPass = render_pass;
        pipeline_info.subpass = 0;
        check_vk(vkCreateGraphicsPipelines(device,
                                           VK_NULL_HANDLE,
                                           1,
                                           &pipeline_info,
                                           nullptr,
                                           &m_texture_pipeline),
                 "vkCreateGraphicsPipelines (imaging texture)");

        vkDestroyShaderModule(device, fragment_shader, nullptr);
        vkDestroyShaderModule(device, vertex_shader, nullptr);

        VkBufferCreateInfo vertex_buffer_info{};
        vertex_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertex_buffer_info.size = sizeof(TextureVertex) * 6;
        vertex_buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        vertex_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check_vk(vkCreateBuffer(device, &vertex_buffer_info, nullptr, &m_texture_vertex_buffer),
                 "vkCreateBuffer (imaging texture vertex)");

        VkMemoryRequirements vertex_memory_requirements{};
        vkGetBufferMemoryRequirements(device, m_texture_vertex_buffer, &vertex_memory_requirements);

        VkMemoryAllocateInfo vertex_allocate_info{};
        vertex_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        vertex_allocate_info.allocationSize = vertex_memory_requirements.size;
        vertex_allocate_info.memoryTypeIndex = find_memory_type(
            m_context.get_physical_device(),
            vertex_memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check_vk(vkAllocateMemory(device, &vertex_allocate_info, nullptr, &m_texture_vertex_memory),
                 "vkAllocateMemory (imaging texture vertex)");

        check_vk(vkBindBufferMemory(device, m_texture_vertex_buffer, m_texture_vertex_memory, 0),
                 "vkBindBufferMemory (imaging texture vertex)");

        check_vk(vkMapMemory(device,
                             m_texture_vertex_memory,
                             0,
                             vertex_buffer_info.size,
                             0,
                             &m_texture_vertex_mapped),
                 "vkMapMemory (imaging texture vertex)");

        ensure_target_initialized();
        update_live_preview_objects();
    }

    ImagingTab::~ImagingTab()
    {
        const VkDevice device = m_context.get_device();

        m_context.wait_idle();

        if (m_texture_vertex_mapped != nullptr)
        {
            vkUnmapMemory(device, m_texture_vertex_memory);
            m_texture_vertex_mapped = nullptr;
        }

        if (m_texture_vertex_buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, m_texture_vertex_buffer, nullptr);
            m_texture_vertex_buffer = VK_NULL_HANDLE;
        }
        if (m_texture_vertex_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_texture_vertex_memory, nullptr);
            m_texture_vertex_memory = VK_NULL_HANDLE;
        }

        if (m_display_staging_mapped != nullptr)
        {
            vkUnmapMemory(device, m_display_staging_memory);
            m_display_staging_mapped = nullptr;
        }

        if (m_display_staging_buffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, m_display_staging_buffer, nullptr);
            m_display_staging_buffer = VK_NULL_HANDLE;
        }
        if (m_display_staging_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_display_staging_memory, nullptr);
            m_display_staging_memory = VK_NULL_HANDLE;
        }

        if (m_display_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_display_sampler, nullptr);
            m_display_sampler = VK_NULL_HANDLE;
        }
        if (m_display_image_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, m_display_image_view, nullptr);
            m_display_image_view = VK_NULL_HANDLE;
        }
        if (m_display_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, m_display_image, nullptr);
            m_display_image = VK_NULL_HANDLE;
        }
        if (m_display_image_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_display_image_memory, nullptr);
            m_display_image_memory = VK_NULL_HANDLE;
        }

        if (m_texture_pipeline != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(device, m_texture_pipeline, nullptr);
            m_texture_pipeline = VK_NULL_HANDLE;
        }
        if (m_texture_pipeline_layout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(device, m_texture_pipeline_layout, nullptr);
            m_texture_pipeline_layout = VK_NULL_HANDLE;
        }

        if (m_texture_descriptor_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(device, m_texture_descriptor_pool, nullptr);
            m_texture_descriptor_pool = VK_NULL_HANDLE;
        }
        if (m_texture_descriptor_set_layout != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorSetLayout(device, m_texture_descriptor_set_layout, nullptr);
            m_texture_descriptor_set_layout = VK_NULL_HANDLE;
        }
    }

    void ImagingTab::update(f64 /*delta_time*/)
    {
        ensure_target_initialized();
        update_live_preview_objects();
        update_session_state();
    }

    void ImagingTab::render(VkCommandBuffer cmd, const shell::ViewportRect& viewport)
    {
        if (!viewport.is_valid())
        {
            return;
        }

        m_last_viewport = viewport;

        apply_viewport(cmd, viewport);
        clear_background(cmd, viewport);

        const UiLayout layout = build_layout(viewport);

        m_line_renderer->begin_frame();
        render_left_column(cmd, viewport, layout);
        render_right_column(viewport, layout);
        shell::apply_full_viewport_pane_scissor(cmd, m_swapchain.get_extent(), viewport);
        m_line_renderer->render(cmd);
    }

    void ImagingTab::on_input(const shell::InputEvent& event, const shell::ViewportRect& viewport)
    {
        if (!event.was_click || !viewport.is_valid())
        {
            return;
        }

        const UiLayout layout = build_layout(viewport);
        static_cast<void>(handle_click(event, layout));
    }

    void ImagingTab::ensure_target_initialized()
    {
        if (m_target.object_id != 0)
        {
            return;
        }

        adopt_target_from_selection();
    }

    void ImagingTab::adopt_target_from_selection()
    {
        if (!m_selection.has_selection())
        {
            return;
        }

        const SelectedObject& selected = m_selection.get_selection();
        if (selected.celestial_obj.id == 0)
        {
            return;
        }

        m_target.object_id = selected.celestial_obj.id;
        m_target.ra_rad = selected.celestial_obj.ra;
        m_target.dec_rad = selected.celestial_obj.dec;

        const std::string_view display_name = m_universe.get_name(m_target.object_id);
        if (!display_name.empty())
        {
            m_target.name = std::string{display_name};
        }
        else
        {
            m_target.name = std::format("OBJ {}", m_target.object_id);
        }

        m_last_image_elapsed_s = -1.0;
    }

    void ImagingTab::set_target(const u64 object_id)
    {
        if (object_id == 0)
        {
            return;
        }

        const std::optional<universe::CelestialObject> obj = m_universe.query_object(object_id);
        if (!obj.has_value())
        {
            return;
        }

        m_target.object_id = obj->id;
        m_target.ra_rad    = obj->ra;
        m_target.dec_rad   = obj->dec;

        const std::string_view display_name = m_universe.get_name(object_id);
        m_target.name = display_name.empty()
            ? std::format("OBJ {}", object_id)
            : std::string{display_name};

        m_last_image_elapsed_s = -1.0;
    }


    void ImagingTab::update_live_preview_objects()
    {
        if (m_target.object_id == 0)
        {
            m_live_preview_objects.clear();
            return;
        }

        const f64 query_radius_deg = std::max(0.01, (m_instrument.get_fov_arcsec() * std::sqrt(2.0) * 0.5) / 3600.0);
        m_universe.query_fov(
            m_target.ra_rad,
            m_target.dec_rad,
            query_radius_deg,
            20.0f,
            universe::QueryFlags::All,
            m_live_preview_objects);
    }

    void ImagingTab::update_session_state()
    {
        if (!m_active_session_id.has_value())
        {
            return;
        }

        const u64 session_id = m_active_session_id.value();
        const auto active_sessions = m_scheduler.get_active();
        const observation::ObservationSession* matched_session = nullptr;

        for (const auto* session : active_sessions)
        {
            if (session != nullptr && session->id() == session_id)
            {
                matched_session = session;
                break;
            }
        }

        if (matched_session == nullptr)
        {
            m_active_session_id.reset();
            m_session_paused = false;
            return;
        }

        m_last_session_snr = matched_session->progress().accumulated_snr;

        if (!m_session_paused)
        {
            refresh_integrated_image();
        }
    }

    void ImagingTab::refresh_integrated_image()
    {
        if (!m_active_session_id.has_value())
        {
            return;
        }

        const u64 session_id = m_active_session_id.value();
        const auto active_sessions = m_scheduler.get_active();

        for (const auto* session : active_sessions)
        {
            if (session == nullptr || session->id() != session_id)
            {
                continue;
            }

            const f64 elapsed_s = std::max(0.0, session->progress().elapsed_hours * 3600.0);
            if (std::abs(elapsed_s - m_last_image_elapsed_s) < 1.0)
            {
                return;
            }

            const u64 seed = session_id ^ kImageSeedSalt;
            m_integrated_image = std::make_unique<imaging::MultispectralImage>(
                session->form_image(m_instrument, m_universe, *m_object_source, seed));
            m_last_image_elapsed_s = elapsed_s;

            const std::vector<u8> rgba8_pixels = build_display_pixels_rgba8();
            if (!rgba8_pixels.empty())
            {
                upload_display_texture(rgba8_pixels,
                                       m_integrated_image->width(),
                                       m_integrated_image->height());
            }

            return;
        }
    }

    void ImagingTab::handle_session_control(const SessionControl action)
    {
        switch (action)
        {
            case SessionControl::Start:
            {
                if (m_active_session_id.has_value())
                {
                    m_session_paused = false;
                    return;
                }

                ensure_target_initialized();
                if (m_target.object_id == 0)
                {
                    PLX_CORE_WARN("ImagingTab START ignored: no target selected");
                    return;
                }

                observation::SessionParameters params{};
                params.type = observation::SessionType::PointedObservation;
                params.target_object_id = m_target.object_id;
                params.target_region.center_ra = m_target.ra_rad;
                params.target_region.center_dec = m_target.dec_rad;
                params.target_region.radius_rad = (m_instrument.get_fov_arcsec() * astro_constants::kArcSecToRad) * 0.5;
                params.instrument_id = m_instrument.get_id();
                params.planned_duration_hours = 8.0;
                params.start_julian_date = m_julian_date;
                params.technique = "imaging_total_power";

                m_active_session_id = m_scheduler.schedule(params);
                m_session_paused = false;
                m_last_image_elapsed_s = -1.0;
                m_last_session_snr = 0.0;
                PLX_CORE_INFO("Imaging session {} started for target {}", m_active_session_id.value(), m_target.object_id);
                return;
            }

            case SessionControl::Pause:
            {
                if (!m_active_session_id.has_value())
                {
                    return;
                }

                m_session_paused = !m_session_paused;
                return;
            }

            case SessionControl::Stop:
            {
                if (!m_active_session_id.has_value())
                {
                    return;
                }

                m_scheduler.abort(m_active_session_id.value());
                m_active_session_id.reset();
                m_session_paused = false;
                return;
            }
        }
    }

    void ImagingTab::handle_save_action(const SaveAction action)
    {
        if (action == SaveAction::Fits)
        {
            PLX_CORE_INFO("SAVE FITS is disabled in Sprint 10a");
            return;
        }

        if (!m_integrated_image || m_integrated_image->band_count() == 0)
        {
            PLX_CORE_WARN("SAVE PNG ignored: no integrated image available");
            return;
        }

        const auto band_plane_index = find_display_band_plane();
        if (!band_plane_index.has_value())
        {
            return;
        }

        const std::filesystem::path exports_dir = core::user_data_save_dir() / "exports";
        const std::filesystem::path filename = std::format(
            "imaging_{}_session_{}.png",
            m_target.object_id,
            m_active_session_id.value_or(0));

        try
        {
            const std::filesystem::path output_path = imaging::ImageExporter::export_png_single_band(
                m_integrated_image->band(*band_plane_index),
                filename,
                to_export_stretch_mode(),
                {},
                exports_dir);
            PLX_CORE_INFO("Saved imaging PNG: {}", output_path.string());
        }
        catch (const std::exception& exception)
        {
            PLX_CORE_ERROR("Failed to save imaging PNG: {}", exception.what());
        }
    }

    ImagingTab::UiLayout ImagingTab::build_layout(const shell::ViewportRect& viewport) const
    {
        UiLayout layout{};
        layout.root = viewport;

        const i32 inner_x = static_cast<i32>(viewport.x) + static_cast<i32>(kOuterPadding);
        const i32 inner_y = static_cast<i32>(viewport.y) + static_cast<i32>(kOuterPadding);
        const i32 inner_w = static_cast<i32>(viewport.width) - static_cast<i32>(2 * kOuterPadding);
        const i32 inner_h = static_cast<i32>(viewport.height) - static_cast<i32>(2 * kOuterPadding);

        const i32 left_w = std::max(180, (inner_w * 53) / 100);
        const i32 right_w = std::max(180, inner_w - left_w - static_cast<i32>(kColumnGap));

        layout.left_column = make_rect(inner_x, inner_y, left_w, inner_h);
        layout.right_column = make_rect(inner_x + left_w + static_cast<i32>(kColumnGap), inner_y, right_w, inner_h);

        i32 left_cursor_y = static_cast<i32>(layout.left_column.y);
        layout.live_preview_rect = make_rect(
            static_cast<i32>(layout.left_column.x),
            left_cursor_y,
            static_cast<i32>(layout.left_column.width),
            static_cast<i32>(kPreviewHeight));
        left_cursor_y += static_cast<i32>(kPreviewHeight + kSectionGap);

        const i32 controls_h = static_cast<i32>(2 * kControlRowHeight + kSectionGap);
        const i32 image_h = std::max(120,
                                     static_cast<i32>(layout.left_column.bottom())
                                         - left_cursor_y
                                         - controls_h);

        layout.integrated_image_rect = make_rect(
            static_cast<i32>(layout.left_column.x),
            left_cursor_y,
            static_cast<i32>(layout.left_column.width),
            image_h);

        left_cursor_y += image_h + static_cast<i32>(kSectionGap);
        const i32 control_w = static_cast<i32>(layout.left_column.width);
        layout.band_cycle_button = make_rect(
            static_cast<i32>(layout.left_column.x),
            left_cursor_y,
            control_w,
            static_cast<i32>(kControlRowHeight));
        left_cursor_y += static_cast<i32>(kControlRowHeight + kSectionGap);
        layout.stretch_cycle_button = make_rect(
            static_cast<i32>(layout.left_column.x),
            left_cursor_y,
            control_w,
            static_cast<i32>(kControlRowHeight));

        i32 right_cursor_y = static_cast<i32>(layout.right_column.y);
        layout.change_target_button = make_rect(
            static_cast<i32>(layout.right_column.x) + static_cast<i32>(layout.right_column.width) - 140,
            right_cursor_y,
            140,
            static_cast<i32>(kButtonHeight));
        right_cursor_y += static_cast<i32>(kButtonHeight + kSectionGap);

        const auto stations = m_instrument.get_stations();
        layout.station_rows.reserve(stations.size());
        for (std::size_t index = 0; index < stations.size(); ++index)
        {
            layout.station_rows.push_back(make_rect(
                static_cast<i32>(layout.right_column.x),
                right_cursor_y,
                static_cast<i32>(layout.right_column.width),
                static_cast<i32>(kControlRowHeight)));
            right_cursor_y += static_cast<i32>(kControlRowHeight);
        }
        right_cursor_y += static_cast<i32>(kSectionGap);

        const auto bands = m_instrument.get_bands();
        layout.band_rows.reserve(bands.size());
        for (std::size_t index = 0; index < bands.size(); ++index)
        {
            layout.band_rows.push_back(make_rect(
                static_cast<i32>(layout.right_column.x),
                right_cursor_y,
                static_cast<i32>(layout.right_column.width),
                static_cast<i32>(kControlRowHeight)));
            right_cursor_y += static_cast<i32>(kControlRowHeight);
        }
        right_cursor_y += static_cast<i32>(kSectionGap);

        layout.progress_bar = make_rect(
            static_cast<i32>(layout.right_column.x),
            right_cursor_y,
            static_cast<i32>(layout.right_column.width),
            14);
        right_cursor_y += static_cast<i32>(14 + kSectionGap);

        const i32 button_w = std::max(70, (static_cast<i32>(layout.right_column.width) - 2 * static_cast<i32>(kSectionGap)) / 3);
        layout.start_button = make_rect(
            static_cast<i32>(layout.right_column.x),
            right_cursor_y,
            button_w,
            static_cast<i32>(kButtonHeight));
        layout.pause_button = make_rect(
            static_cast<i32>(layout.start_button.right()) + static_cast<i32>(kSectionGap),
            right_cursor_y,
            button_w,
            static_cast<i32>(kButtonHeight));
        layout.stop_button = make_rect(
            static_cast<i32>(layout.pause_button.right()) + static_cast<i32>(kSectionGap),
            right_cursor_y,
            std::max(40, static_cast<i32>(layout.right_column.right()) - static_cast<i32>(layout.pause_button.right()) - static_cast<i32>(kSectionGap)),
            static_cast<i32>(kButtonHeight));

        right_cursor_y += static_cast<i32>(kButtonHeight + kSectionGap);
        const i32 save_w = (static_cast<i32>(layout.right_column.width) - static_cast<i32>(kSectionGap)) / 2;
        layout.save_fits_button = make_rect(
            static_cast<i32>(layout.right_column.x),
            right_cursor_y,
            save_w,
            static_cast<i32>(kButtonHeight));
        layout.save_png_button = make_rect(
            static_cast<i32>(layout.save_fits_button.right()) + static_cast<i32>(kSectionGap),
            right_cursor_y,
            std::max(40, static_cast<i32>(layout.right_column.right()) - static_cast<i32>(layout.save_fits_button.right()) - static_cast<i32>(kSectionGap)),
            static_cast<i32>(kButtonHeight));

        return layout;
    }

    void ImagingTab::render_left_column(VkCommandBuffer cmd,
                                        const shell::ViewportRect& viewport,
                                        const UiLayout& layout)
    {
        draw_filled_rect(*m_line_renderer, layout.live_preview_rect, kPanelBackground, m_swapchain.get_extent());
        draw_rect_outline(*m_line_renderer, layout.live_preview_rect, kPanelBorder, m_swapchain.get_extent());

        draw_filled_rect(*m_line_renderer, layout.integrated_image_rect, kPanelBackground, m_swapchain.get_extent());
        draw_rect_outline(*m_line_renderer, layout.integrated_image_rect, kPanelBorder, m_swapchain.get_extent());

        draw_rect_outline(*m_line_renderer,
                          layout.band_cycle_button,
                          kPanelBorderBright,
                          m_swapchain.get_extent());
        draw_rect_outline(*m_line_renderer,
                          layout.stretch_cycle_button,
                          kPanelBorderBright,
                          m_swapchain.get_extent());

        m_font.draw_text("LIVE PREVIEW",
                         static_cast<f32>(layout.live_preview_rect.x + 4),
                         static_cast<f32>(layout.live_preview_rect.y + 2),
                         1.0f,
                         kTextBright);

        m_font.draw_text("INTEGRATED IMAGE",
                         static_cast<f32>(layout.integrated_image_rect.x + 4),
                         static_cast<f32>(layout.integrated_image_rect.y + 2),
                         1.0f,
                         kTextBright);

        const auto band_plane_index = find_display_band_plane();
        std::string band_label = "Band: [None]";
        if (m_integrated_image && band_plane_index.has_value())
        {
            band_label = std::format("Band: [{} \\x1F]", m_integrated_image->band(*band_plane_index).metadata().band_name);
        }
        m_font.draw_text(band_label,
                         static_cast<f32>(layout.band_cycle_button.x + 4),
                         static_cast<f32>(layout.band_cycle_button.y + 2),
                         1.0f,
                         kTextBright);

        std::string stretch_name = "linear";
        if (m_selected_stretch_mode == StretchMode::Log)
        {
            stretch_name = "log";
        }
        else if (m_selected_stretch_mode == StretchMode::Asinh)
        {
            stretch_name = "asinh";
        }

        m_font.draw_text(std::format("Stretch: [{} \\x1F]", stretch_name),
                         static_cast<f32>(layout.stretch_cycle_button.x + 4),
                         static_cast<f32>(layout.stretch_cycle_button.y + 2),
                         1.0f,
                         kTextBright);

        render_live_preview(viewport, layout.live_preview_rect);
        render_integrated_image(cmd, viewport, layout.integrated_image_rect);
    }

    void ImagingTab::render_right_column(const shell::ViewportRect& /*viewport*/, const UiLayout& layout)
    {
        const auto stations = m_instrument.get_stations();
        const auto bands = m_instrument.get_bands();

        const std::string target_line = std::format("Target: {}", m_target.name);
        m_font.draw_text(target_line,
                         static_cast<f32>(layout.right_column.x),
                         static_cast<f32>(layout.right_column.y),
                         1.0f,
                         kTextBright);
        m_font.draw_text(format_ra_dec_text(m_target.ra_rad, m_target.dec_rad),
                         static_cast<f32>(layout.right_column.x),
                         static_cast<f32>(layout.right_column.y + kFontGlyphH),
                         1.0f,
                         kTextDim);

        draw_rect_outline(*m_line_renderer,
                          layout.change_target_button,
                          kPanelBorderBright,
                          m_swapchain.get_extent());
        m_font.draw_text("CHANGE TARGET",
                         static_cast<f32>(layout.change_target_button.x + 4),
                         static_cast<f32>(layout.change_target_button.y + 2),
                         1.0f,
                         kTextBright);

        if (!layout.station_rows.empty())
        {
            const std::string station_header = std::format("Stations ({}/{} active)",
                                                           m_instrument.get_active_station_count(),
                                                           stations.size());
            m_font.draw_text(station_header,
                             static_cast<f32>(layout.station_rows.front().x),
                             static_cast<f32>(static_cast<i32>(layout.station_rows.front().y) - kFontGlyphH),
                             1.0f,
                             kTextBright);
        }

        for (std::size_t index = 0; index < layout.station_rows.size(); ++index)
        {
            const shell::ViewportRect row = layout.station_rows[index];
            draw_rect_outline(*m_line_renderer, row, kPanelBorder, m_swapchain.get_extent());

            const bool active = stations[index].is_active;
            const std::string label = std::format("{} {} ({:.1f}m)",
                                                  active ? "[x]" : "[ ]",
                                                  stations[index].name,
                                                  stations[index].aperture_diameter_m);
            m_font.draw_text(label,
                             static_cast<f32>(row.x + 4),
                             static_cast<f32>(row.y + 2),
                             1.0f,
                             active ? kTextBright : kTextDim);
        }

        if (!layout.band_rows.empty())
        {
            m_font.draw_text("Bands",
                             static_cast<f32>(layout.band_rows.front().x),
                             static_cast<f32>(static_cast<i32>(layout.band_rows.front().y) - kFontGlyphH),
                             1.0f,
                             kTextBright);
        }

        for (std::size_t index = 0; index < layout.band_rows.size(); ++index)
        {
            const shell::ViewportRect row = layout.band_rows[index];
            draw_rect_outline(*m_line_renderer, row, kPanelBorder, m_swapchain.get_extent());

            const bool unlocked = bands[index].is_unlocked;
            const bool active = bands[index].is_active;
            const std::string prefix = unlocked ? (active ? "[x]" : "[ ]") : "[\\x12]";
            const std::string label = std::format("{} {} ({:.0f}nm)",
                                                  prefix,
                                                  bands[index].name,
                                                  bands[index].center_wavelength_nm);
            m_font.draw_text(label,
                             static_cast<f32>(row.x + 4),
                             static_cast<f32>(row.y + 2),
                             1.0f,
                             unlocked ? (active ? kTextBright : kTextDim) : kTextDisabled);
        }

        const f64 display_wavelength_nm = [&]() -> f64
        {
            const auto active_band_indices = m_instrument.get_active_bands();
            if (active_band_indices.empty() || active_band_indices.front() >= bands.size())
            {
                return 550.0;
            }
            return bands[active_band_indices.front()].center_wavelength_nm;
        }();

        const f64 resolution_arcsec = m_instrument.get_angular_resolution_arcsec(display_wavelength_nm);
        const f64 resolution_mas = resolution_arcsec * 1000.0;
        const i32 readout_x = static_cast<i32>(layout.progress_bar.x);
        const i32 readout_base_y = static_cast<i32>(layout.progress_bar.y);
        m_font.draw_text(std::format("Resolution: {:.2f} mas ({:.4f}\")", resolution_mas, resolution_arcsec),
                         static_cast<f32>(readout_x),
                         static_cast<f32>(readout_base_y - 4 * kFontGlyphH),
                         1.0f,
                         kTextBright);

        m_font.draw_text(std::format("Collecting area: {:.1f} m2", m_instrument.get_total_collecting_area_m2()),
                         static_cast<f32>(readout_x),
                         static_cast<f32>(readout_base_y - 3 * kFontGlyphH),
                         1.0f,
                         kTextBright);

        const f64 elapsed_hours = std::max(0.0, m_last_image_elapsed_s / 3600.0);
        m_font.draw_text(std::format("Elapsed: {:.2f} h", elapsed_hours),
                         static_cast<f32>(readout_x),
                         static_cast<f32>(readout_base_y - 2 * kFontGlyphH),
                         1.0f,
                         kTextBright);

        m_font.draw_text(std::format("SNR: {:.2f}", m_last_session_snr),
                         static_cast<f32>(readout_x),
                         static_cast<f32>(readout_base_y - kFontGlyphH),
                         1.0f,
                         kTextBright);

        draw_rect_outline(*m_line_renderer,
                          layout.progress_bar,
                          kPanelBorderBright,
                          m_swapchain.get_extent());

        const f64 progress_fraction = std::clamp(m_last_session_snr / kTargetSnr, 0.0, 1.0);
        const i32 fill_width = static_cast<i32>(std::round(progress_fraction * static_cast<f64>(layout.progress_bar.width)));
        if (fill_width > 0)
        {
            const shell::ViewportRect fill_rect = make_rect(
                static_cast<i32>(layout.progress_bar.x),
                static_cast<i32>(layout.progress_bar.y),
                fill_width,
                static_cast<i32>(layout.progress_bar.height));
            draw_filled_rect(*m_line_renderer, fill_rect, kProgressFill, m_swapchain.get_extent());
        }

        m_font.draw_text(std::format("target SNR {:.0f}", kTargetSnr),
                         static_cast<f32>(layout.progress_bar.x + 4),
                         static_cast<f32>(layout.progress_bar.y + 1),
                         1.0f,
                         kTextBright);

        const auto draw_button = [&](const shell::ViewportRect& rect,
                                     const std::string_view label,
                                     const bool enabled)
        {
            draw_rect_outline(*m_line_renderer,
                              rect,
                              enabled ? kPanelBorderBright : kPanelBorder,
                              m_swapchain.get_extent());
            m_font.draw_text(std::string{label},
                             static_cast<f32>(rect.x + 4),
                             static_cast<f32>(rect.y + 2),
                             1.0f,
                             enabled ? kTextBright : kTextDisabled);
        };

        draw_button(layout.start_button, "START", true);
        draw_button(layout.pause_button, m_session_paused ? "RESUME" : "PAUSE", m_active_session_id.has_value());
        draw_button(layout.stop_button, "STOP", m_active_session_id.has_value());

        draw_button(layout.save_fits_button, "SAVE FITS", false);
        draw_button(layout.save_png_button, "SAVE PNG", m_integrated_image && m_integrated_image->band_count() > 0);

    }

    void ImagingTab::render_live_preview(const shell::ViewportRect& viewport,
                                         const shell::ViewportRect& preview_rect)
    {
        static_cast<void>(viewport);

        const i32 content_margin = 18;
        const shell::ViewportRect field_rect = make_rect(
            static_cast<i32>(preview_rect.x) + content_margin,
            static_cast<i32>(preview_rect.y) + content_margin,
            static_cast<i32>(preview_rect.width) - 2 * content_margin,
            static_cast<i32>(preview_rect.height) - 2 * content_margin);

        draw_rect_outline(*m_line_renderer, field_rect, kPanelBorderBright, m_swapchain.get_extent());

        const f64 fov_arcsec = std::max(1.0, m_instrument.get_fov_arcsec());
        const f64 pixel_scale = fov_arcsec / static_cast<f64>(std::max(field_rect.width, 1u));

        const Vec2f window_size{
            static_cast<f32>(m_swapchain.get_extent().width),
            static_cast<f32>(m_swapchain.get_extent().height),
        };

        const f64 center_x = static_cast<f64>(field_rect.x) + static_cast<f64>(field_rect.width) * 0.5;
        const f64 center_y = static_cast<f64>(field_rect.y) + static_cast<f64>(field_rect.height) * 0.5;

        constexpr f32 marker_radius = 6.0f;
        m_line_renderer->add_line(
            pixel_to_ndc({static_cast<f32>(center_x - marker_radius), static_cast<f32>(center_y)}, window_size),
            pixel_to_ndc({static_cast<f32>(center_x + marker_radius), static_cast<f32>(center_y)}, window_size),
            kMarkerColor);
        m_line_renderer->add_line(
            pixel_to_ndc({static_cast<f32>(center_x), static_cast<f32>(center_y - marker_radius)}, window_size),
            pixel_to_ndc({static_cast<f32>(center_x), static_cast<f32>(center_y + marker_radius)}, window_size),
            kMarkerColor);

        for (const auto& object : m_live_preview_objects)
        {
            const auto [x_px, y_px] = project_gnomonic(
                object.ra,
                object.dec,
                m_target.ra_rad,
                m_target.dec_rad,
                pixel_scale,
                field_rect.width,
                field_rect.height);

            if (!std::isfinite(x_px) || !std::isfinite(y_px))
            {
                continue;
            }

            const f64 screen_x = static_cast<f64>(field_rect.x) + x_px;
            const f64 screen_y = static_cast<f64>(field_rect.y) + y_px;
            if (screen_x < static_cast<f64>(field_rect.x)
                || screen_x >= static_cast<f64>(field_rect.right())
                || screen_y < static_cast<f64>(field_rect.y)
                || screen_y >= static_cast<f64>(field_rect.bottom()))
            {
                continue;
            }

            const f32 dot = 2.0f;
            m_line_renderer->add_line(
                pixel_to_ndc({static_cast<f32>(screen_x - dot), static_cast<f32>(screen_y)}, window_size),
                pixel_to_ndc({static_cast<f32>(screen_x + dot), static_cast<f32>(screen_y)}, window_size),
                kObjectColor);
            m_line_renderer->add_line(
                pixel_to_ndc({static_cast<f32>(screen_x), static_cast<f32>(screen_y - dot)}, window_size),
                pixel_to_ndc({static_cast<f32>(screen_x), static_cast<f32>(screen_y + dot)}, window_size),
                kObjectColor);
        }

        m_font.draw_text(std::format("FOV: {:.0f}\"", fov_arcsec),
                         static_cast<f32>(field_rect.x + 4),
                         static_cast<f32>(static_cast<i32>(field_rect.bottom()) - kFontGlyphH),
                         1.0f,
                         kTextBright);
    }

    void ImagingTab::render_integrated_image(VkCommandBuffer cmd,
                                             const shell::ViewportRect& viewport,
                                             const shell::ViewportRect& image_rect)
    {
        if (!m_integrated_image || m_integrated_image->band_count() == 0)
        {
            m_font.draw_text("No integration running",
                             static_cast<f32>(image_rect.x + 8),
                             static_cast<f32>(image_rect.y + 24),
                             1.0f,
                             kTextDim);
            return;
        }

        const std::vector<u8> rgba8_pixels = build_display_pixels_rgba8();
        if (!rgba8_pixels.empty())
        {
            upload_display_texture(rgba8_pixels, m_integrated_image->width(), m_integrated_image->height());
        }

        if (m_display_image == VK_NULL_HANDLE
            || m_display_image_view == VK_NULL_HANDLE
            || m_display_sampler == VK_NULL_HANDLE)
        {
            return;
        }

        const std::array<TextureVertex, 6> quad_vertices{
            TextureVertex{{static_cast<f32>(image_rect.x), static_cast<f32>(image_rect.y)}, {0.0f, 0.0f}},
            TextureVertex{{static_cast<f32>(image_rect.x), static_cast<f32>(image_rect.bottom())}, {0.0f, 1.0f}},
            TextureVertex{{static_cast<f32>(image_rect.right()), static_cast<f32>(image_rect.bottom())}, {1.0f, 1.0f}},
            TextureVertex{{static_cast<f32>(image_rect.x), static_cast<f32>(image_rect.y)}, {0.0f, 0.0f}},
            TextureVertex{{static_cast<f32>(image_rect.right()), static_cast<f32>(image_rect.bottom())}, {1.0f, 1.0f}},
            TextureVertex{{static_cast<f32>(image_rect.right()), static_cast<f32>(image_rect.y)}, {1.0f, 0.0f}},
        };

        std::memcpy(m_texture_vertex_mapped, quad_vertices.data(), sizeof(quad_vertices));

        apply_viewport(cmd, viewport);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_texture_pipeline);
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_texture_pipeline_layout,
                                0,
                                1,
                                &m_texture_descriptor_set,
                                0,
                                nullptr);

        const auto extent = m_swapchain.get_extent();
        TextPushConstants push_constants{
            .viewport_w = static_cast<f32>(extent.width),
            .viewport_h = static_cast<f32>(extent.height),
        };

        vkCmdPushConstants(cmd,
                           m_texture_pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT,
                           0,
                           sizeof(TextPushConstants),
                           &push_constants);

        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_texture_vertex_buffer, &offset);
        vkCmdDraw(cmd, static_cast<u32>(quad_vertices.size()), 1, 0, 0);

        shell::apply_full_viewport_pane_scissor(cmd, m_swapchain.get_extent(), viewport);
    }

    void ImagingTab::recreate_display_texture_if_needed(const u32 width_px, const u32 height_px)
    {
        if (width_px == 0 || height_px == 0)
        {
            return;
        }

        if (m_display_image != VK_NULL_HANDLE && m_display_image_width == width_px && m_display_image_height == height_px)
        {
            return;
        }

        const VkDevice device = m_context.get_device();

        m_context.wait_idle();

        if (m_display_sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(device, m_display_sampler, nullptr);
            m_display_sampler = VK_NULL_HANDLE;
        }
        if (m_display_image_view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, m_display_image_view, nullptr);
            m_display_image_view = VK_NULL_HANDLE;
        }
        if (m_display_image != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, m_display_image, nullptr);
            m_display_image = VK_NULL_HANDLE;
        }
        if (m_display_image_memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, m_display_image_memory, nullptr);
            m_display_image_memory = VK_NULL_HANDLE;
        }

        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        image_info.extent = {width_px, height_px, 1};
        image_info.mipLevels = 1;
        image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check_vk(vkCreateImage(device, &image_info, nullptr, &m_display_image),
                 "vkCreateImage (imaging display)");

        VkMemoryRequirements image_memory_requirements{};
        vkGetImageMemoryRequirements(device, m_display_image, &image_memory_requirements);

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.allocationSize = image_memory_requirements.size;
        allocate_info.memoryTypeIndex = find_memory_type(
            m_context.get_physical_device(),
            image_memory_requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check_vk(vkAllocateMemory(device, &allocate_info, nullptr, &m_display_image_memory),
                 "vkAllocateMemory (imaging display)");

        check_vk(vkBindImageMemory(device, m_display_image, m_display_image_memory, 0),
                 "vkBindImageMemory (imaging display)");

        VkImageViewCreateInfo view_info{};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.image = m_display_image;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_info.subresourceRange.baseMipLevel = 0;
        view_info.subresourceRange.levelCount = 1;
        view_info.subresourceRange.baseArrayLayer = 0;
        view_info.subresourceRange.layerCount = 1;
        check_vk(vkCreateImageView(device, &view_info, nullptr, &m_display_image_view),
                 "vkCreateImageView (imaging display)");

        VkSamplerCreateInfo sampler_info{};
        sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_info.magFilter = VK_FILTER_LINEAR;
        sampler_info.minFilter = VK_FILTER_LINEAR;
        sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_info.maxAnisotropy = 1.0f;
        check_vk(vkCreateSampler(device, &sampler_info, nullptr, &m_display_sampler),
                 "vkCreateSampler (imaging display)");

        VkDescriptorImageInfo image_descriptor{};
        image_descriptor.sampler = m_display_sampler;
        image_descriptor.imageView = m_display_image_view;
        image_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write_descriptor{};
        write_descriptor.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write_descriptor.dstSet = m_texture_descriptor_set;
        write_descriptor.dstBinding = 0;
        write_descriptor.descriptorCount = 1;
        write_descriptor.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write_descriptor.pImageInfo = &image_descriptor;
        vkUpdateDescriptorSets(device, 1, &write_descriptor, 0, nullptr);

        m_display_image_width = width_px;
        m_display_image_height = height_px;
        m_display_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }

    void ImagingTab::upload_display_texture(const std::span<const u8> rgba8_pixels,
                                            const u32 width_px,
                                            const u32 height_px)
    {
        if (rgba8_pixels.empty() || width_px == 0 || height_px == 0)
        {
            return;
        }

        recreate_display_texture_if_needed(width_px, height_px);

        const VkDevice device = m_context.get_device();
        const VkDeviceSize upload_size = static_cast<VkDeviceSize>(rgba8_pixels.size());

        if (upload_size == 0)
        {
            return;
        }

        if (m_display_staging_buffer == VK_NULL_HANDLE || upload_size > m_display_staging_capacity)
        {
            m_context.wait_idle();

            if (m_display_staging_mapped != nullptr)
            {
                vkUnmapMemory(device, m_display_staging_memory);
                m_display_staging_mapped = nullptr;
            }
            if (m_display_staging_buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device, m_display_staging_buffer, nullptr);
                m_display_staging_buffer = VK_NULL_HANDLE;
            }
            if (m_display_staging_memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, m_display_staging_memory, nullptr);
                m_display_staging_memory = VK_NULL_HANDLE;
            }

            VkBufferCreateInfo staging_buffer_info{};
            staging_buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            staging_buffer_info.size = upload_size;
            staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check_vk(vkCreateBuffer(device, &staging_buffer_info, nullptr, &m_display_staging_buffer),
                     "vkCreateBuffer (imaging staging)");

            VkMemoryRequirements staging_requirements{};
            vkGetBufferMemoryRequirements(device, m_display_staging_buffer, &staging_requirements);

            VkMemoryAllocateInfo staging_allocate_info{};
            staging_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            staging_allocate_info.allocationSize = staging_requirements.size;
            staging_allocate_info.memoryTypeIndex = find_memory_type(
                m_context.get_physical_device(),
                staging_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check_vk(vkAllocateMemory(device,
                                      &staging_allocate_info,
                                      nullptr,
                                      &m_display_staging_memory),
                     "vkAllocateMemory (imaging staging)");

            check_vk(vkBindBufferMemory(device,
                                        m_display_staging_buffer,
                                        m_display_staging_memory,
                                        0),
                     "vkBindBufferMemory (imaging staging)");

            check_vk(vkMapMemory(device,
                                 m_display_staging_memory,
                                 0,
                                 staging_buffer_info.size,
                                 0,
                                 &m_display_staging_mapped),
                     "vkMapMemory (imaging staging)");

            m_display_staging_capacity = upload_size;
        }

        std::memcpy(m_display_staging_mapped, rgba8_pixels.data(), rgba8_pixels.size());

        VkCommandPoolCreateInfo command_pool_info{};
        command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        command_pool_info.queueFamilyIndex = m_context.get_graphics_queue_family();
        command_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        VkCommandPool command_pool = VK_NULL_HANDLE;
        check_vk(vkCreateCommandPool(device, &command_pool_info, nullptr, &command_pool),
                 "vkCreateCommandPool (imaging upload)");

        VkCommandBufferAllocateInfo command_allocate_info{};
        command_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_allocate_info.commandPool = command_pool;
        command_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_allocate_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        check_vk(vkAllocateCommandBuffers(device, &command_allocate_info, &command_buffer),
                 "vkAllocateCommandBuffers (imaging upload)");

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check_vk(vkBeginCommandBuffer(command_buffer, &begin_info),
                 "vkBeginCommandBuffer (imaging upload)");

        VkImageMemoryBarrier to_transfer{};
        to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_transfer.oldLayout = m_display_image_layout;
        to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_transfer.image = m_display_image;
        to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_transfer.subresourceRange.baseMipLevel = 0;
        to_transfer.subresourceRange.levelCount = 1;
        to_transfer.subresourceRange.baseArrayLayer = 0;
        to_transfer.subresourceRange.layerCount = 1;
        to_transfer.srcAccessMask =
            m_display_image_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_READ_BIT;
        to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(command_buffer,
                             m_display_image_layout == VK_IMAGE_LAYOUT_UNDEFINED
                                 ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                 : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &to_transfer);

        VkBufferImageCopy copy_region{};
        copy_region.bufferOffset = 0;
        copy_region.bufferRowLength = 0;
        copy_region.bufferImageHeight = 0;
        copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_region.imageSubresource.mipLevel = 0;
        copy_region.imageSubresource.baseArrayLayer = 0;
        copy_region.imageSubresource.layerCount = 1;
        copy_region.imageOffset = {0, 0, 0};
        copy_region.imageExtent = {width_px, height_px, 1};
        vkCmdCopyBufferToImage(command_buffer,
                               m_display_staging_buffer,
                               m_display_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &copy_region);

        VkImageMemoryBarrier to_shader_read{};
        to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader_read.image = m_display_image;
        to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_shader_read.subresourceRange.baseMipLevel = 0;
        to_shader_read.subresourceRange.levelCount = 1;
        to_shader_read.subresourceRange.baseArrayLayer = 0;
        to_shader_read.subresourceRange.layerCount = 1;
        to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(command_buffer,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &to_shader_read);

        check_vk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer (imaging upload)");

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VkFence upload_fence = VK_NULL_HANDLE;
        check_vk(vkCreateFence(device, &fence_info, nullptr, &upload_fence),
                 "vkCreateFence (imaging upload)");

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        check_vk(vkQueueSubmit(m_context.get_graphics_queue(), 1, &submit_info, upload_fence),
                 "vkQueueSubmit (imaging upload)");
        check_vk(vkWaitForFences(device, 1, &upload_fence, VK_TRUE, UINT64_MAX),
                 "vkWaitForFences (imaging upload)");

        vkDestroyFence(device, upload_fence, nullptr);
        vkDestroyCommandPool(device, command_pool, nullptr);

        m_display_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    std::optional<std::size_t> ImagingTab::find_display_band_plane() const
    {
        if (!m_integrated_image || m_integrated_image->band_count() == 0)
        {
            return std::nullopt;
        }

        for (std::size_t band_index = 0; band_index < m_integrated_image->band_count(); ++band_index)
        {
            if (m_integrated_image->band(band_index).metadata().band_index == m_selected_display_band_index)
            {
                return band_index;
            }
        }

        return 0;
    }

    std::vector<u8> ImagingTab::build_display_pixels_rgba8() const
    {
        if (!m_integrated_image || m_integrated_image->band_count() == 0)
        {
            return {};
        }

        const auto display_band = find_display_band_plane();
        if (!display_band.has_value())
        {
            return {};
        }

        const imaging::Image& band_image = m_integrated_image->band(*display_band);
        const std::vector<u16> stretched = imaging::ImageExporter::stretch_to_u16(
            band_image.pixels(),
            to_export_stretch_mode());

        std::vector<u8> rgba8(stretched.size() * 4, 0);
        for (std::size_t pixel = 0; pixel < stretched.size(); ++pixel)
        {
            const u8 value = static_cast<u8>(stretched[pixel] >> 8);
            rgba8[4 * pixel + 0] = value;
            rgba8[4 * pixel + 1] = value;
            rgba8[4 * pixel + 2] = value;
            rgba8[4 * pixel + 3] = 255;
        }

        return rgba8;
    }

    imaging::StretchMode ImagingTab::to_export_stretch_mode() const
    {
        switch (m_selected_stretch_mode)
        {
            case StretchMode::Linear:
                return imaging::StretchMode::Linear;
            case StretchMode::Log:
                return imaging::StretchMode::Log;
            case StretchMode::Asinh:
                return imaging::StretchMode::Asinh;
        }

        return imaging::StretchMode::Linear;
    }

    void ImagingTab::cycle_stretch_mode()
    {
        switch (m_selected_stretch_mode)
        {
            case StretchMode::Linear:
                m_selected_stretch_mode = StretchMode::Log;
                break;
            case StretchMode::Log:
                m_selected_stretch_mode = StretchMode::Asinh;
                break;
            case StretchMode::Asinh:
                m_selected_stretch_mode = StretchMode::Linear;
                break;
        }
    }

    void ImagingTab::cycle_display_band()
    {
        const auto active_band_indices = m_instrument.get_active_bands();
        if (active_band_indices.empty())
        {
            return;
        }

        const auto found = std::find(active_band_indices.begin(),
                                     active_band_indices.end(),
                                     m_selected_display_band_index);
        if (found == active_band_indices.end())
        {
            m_selected_display_band_index = active_band_indices.front();
            return;
        }

        const std::size_t index = static_cast<std::size_t>(std::distance(active_band_indices.begin(), found));
        const std::size_t next = (index + 1) % active_band_indices.size();
        m_selected_display_band_index = active_band_indices[next];
    }

    bool ImagingTab::handle_click(const shell::InputEvent& event, const UiLayout& layout)
    {
        if (contains_local(layout.change_target_button, event.click_pos))
        {
            adopt_target_from_selection();
            return true;
        }

        if (contains_local(layout.band_cycle_button, event.click_pos))
        {
            cycle_display_band();
            return true;
        }

        if (contains_local(layout.stretch_cycle_button, event.click_pos))
        {
            cycle_stretch_mode();
            return true;
        }

        if (contains_local(layout.start_button, event.click_pos))
        {
            handle_session_control(SessionControl::Start);
            return true;
        }

        if (contains_local(layout.pause_button, event.click_pos))
        {
            handle_session_control(SessionControl::Pause);
            return true;
        }

        if (contains_local(layout.stop_button, event.click_pos))
        {
            handle_session_control(SessionControl::Stop);
            return true;
        }

        if (contains_local(layout.save_png_button, event.click_pos))
        {
            handle_save_action(SaveAction::Png);
            return true;
        }

        if (contains_local(layout.save_fits_button, event.click_pos))
        {
            handle_save_action(SaveAction::Fits);
            return true;
        }

        if (handle_station_click(event, layout))
        {
            return true;
        }

        if (handle_band_click(event, layout))
        {
            return true;
        }

        return false;
    }

    bool ImagingTab::handle_station_click(const shell::InputEvent& event, const UiLayout& layout)
    {
        const auto stations = m_instrument.get_stations();
        for (std::size_t index = 0; index < layout.station_rows.size() && index < stations.size(); ++index)
        {
            if (!contains_local(layout.station_rows[index], event.click_pos))
            {
                continue;
            }

            m_instrument.set_station_active(static_cast<u32>(index), !stations[index].is_active);
            m_last_image_elapsed_s = -1.0;
            return true;
        }

        return false;
    }

    bool ImagingTab::handle_band_click(const shell::InputEvent& event, const UiLayout& layout)
    {
        const auto bands = m_instrument.get_bands();
        for (std::size_t index = 0; index < layout.band_rows.size() && index < bands.size(); ++index)
        {
            if (!contains_local(layout.band_rows[index], event.click_pos))
            {
                continue;
            }

            if (!bands[index].is_unlocked)
            {
                return true;
            }

            m_instrument.set_band_active(static_cast<u32>(index), !bands[index].is_active);
            const auto active_band_indices = m_instrument.get_active_bands();
            if (std::find(active_band_indices.begin(),
                          active_band_indices.end(),
                          m_selected_display_band_index) == active_band_indices.end())
            {
                if (!active_band_indices.empty())
                {
                    m_selected_display_band_index = active_band_indices.front();
                }
            }

            m_last_image_elapsed_s = -1.0;
            return true;
        }

        return false;
    }
}
