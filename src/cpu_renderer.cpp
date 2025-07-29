#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

#include "Image2d.h"

#include "camera.hpp"
#include "cpu_renderer.hpp"
#include "mesh.hpp"
#include "sdf_grid.hpp"
#include "utils.hpp"

static std::vector<uint32_t> read_shader_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open shader file: " + filename);
    }
    size_t file_size = (size_t)file.tellg();
    if (file_size % 4 != 0) {
        throw std::runtime_error("SPIR-V file size is not a multiple of 4: " + filename);
    }
    std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), file_size);
    file.close();
    return buffer;
}

CpuRenderer::CpuRenderer(std::shared_ptr<VulkanContext> a_vulkan_context)
    : vulkan_context(std::move(a_vulkan_context)) {
}

CpuRenderer::~CpuRenderer() {
    shutdown();
}

void CpuRenderer::init(int a_width, int a_height, GLFWwindow* /*window*/) {
    this->width = a_width;
    this->height = a_height;
    create_cpu_render_target(this->width, this->height);
    create_vulkan_display_resources();
}

void CpuRenderer::shutdown() {
    destroy_vulkan_display_resources();
    destroy_cpu_render_target();
}

void CpuRenderer::resize(int a_width, int a_height) {
    if (this->width == a_width && this->height == a_height) {
        return;
    }

    if (this->vulkan_context) {
        vkDeviceWaitIdle(this->vulkan_context->get_device());
        destroy_vulkan_display_resources();
    }

    destroy_cpu_render_target();

    this->width = a_width;
    this->height = a_height;
    create_cpu_render_target(this->width, this->height);
    create_vulkan_display_resources();
}

void CpuRenderer::create_cpu_render_target(int a_width, int a_height) {
    this->framebuffer.resize(static_cast<size_t>(a_width) * a_height);
}

void CpuRenderer::destroy_cpu_render_target() {
    this->framebuffer.clear();
}

void CpuRenderer::create_vulkan_display_resources() {
    if (!this->vulkan_context) {
        return;
    }

    this->vulkan_context->create_image_and_view(
        this->width, this->height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        cpu_texture_image_, cpu_texture_image_memory_, cpu_texture_image_view_
    );

    // Создаем промежуточный (staging) буфер для передачи данных из CPU в GPU
    this->vulkan_context->create_buffer(
        this->width * this->height * sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, // Буфер будет источником для копирования
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, // Видим для CPU и когерентен
        staging_buffer_, staging_buffer_memory_
    );

    // Создаем семплер для текстуры
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.mipLodBias = 0.0f;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;

    MY_VK_CHECK(vkCreateSampler(this->vulkan_context->get_device(), &sampler_info, nullptr, &cpu_texture_sampler_));

    // Создаем layout для набора дескрипторов (для текстуры)
    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;
    sampler_binding.descriptorCount = 1;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.pImmutableSamplers = nullptr;
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &sampler_binding;

    MY_VK_CHECK(vkCreateDescriptorSetLayout(this->vulkan_context->get_device(), &layout_info, nullptr, &descriptor_set_layout_));

    // Создаем пул дескрипторов
    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    pool_info.maxSets = 1;
    MY_VK_CHECK(vkCreateDescriptorPool(this->vulkan_context->get_device(), &pool_info, nullptr, &descriptor_pool_));

    // Выделяем набор дескрипторов
    VkDescriptorSetAllocateInfo alloc_set_info{};
    alloc_set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_set_info.descriptorPool = descriptor_pool_;
    alloc_set_info.descriptorSetCount = 1;
    alloc_set_info.pSetLayouts = &descriptor_set_layout_;
    MY_VK_CHECK(vkAllocateDescriptorSets(this->vulkan_context->get_device(), &alloc_set_info, &descriptor_set_));

    // Обновляем дескриптор, связывая его с нашей текстурой и семплером
    VkDescriptorImageInfo image_info_descriptor{};
    image_info_descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    image_info_descriptor.imageView = cpu_texture_image_view_;
    image_info_descriptor.sampler = cpu_texture_sampler_;

    VkWriteDescriptorSet descriptor_write{};
    descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptor_write.dstSet = descriptor_set_;
    descriptor_write.dstBinding = 0;
    descriptor_write.dstArrayElement = 0;
    descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptor_write.descriptorCount = 1;
    descriptor_write.pImageInfo = &image_info_descriptor;

    vkUpdateDescriptorSets(this->vulkan_context->get_device(), 1, &descriptor_write, 0, nullptr);

    // Создаем pipeline layout
    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout_;
    MY_VK_CHECK(vkCreatePipelineLayout(this->vulkan_context->get_device(), &pipeline_layout_info, nullptr, &pipeline_layout_));

    // Создаем графический пайплайн для отрисовки квада с текстурой
    auto vert_shader_code = read_shader_file("assets/shaders/quad.vert.spv");
    auto frag_shader_code = read_shader_file("assets/shaders/quad.frag.spv");

    VkShaderModule vert_shader_module = vk_utils::createShaderModule(this->vulkan_context->get_device(), vert_shader_code);
    VkShaderModule frag_shader_module = vk_utils::createShaderModule(this->vulkan_context->get_device(), frag_shader_code);

    VkPipelineShaderStageCreateInfo vert_shader_stage_info{};
    vert_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vert_shader_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vert_shader_stage_info.module = vert_shader_module;
    vert_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo frag_shader_stage_info{};
    frag_shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    frag_shader_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    frag_shader_stage_info.module = frag_shader_module;
    frag_shader_stage_info.pName = "main";

    VkPipelineShaderStageCreateInfo shader_stages[] = {vert_shader_stage_info, frag_shader_stage_info};

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount = 0; // Для полноэкранного квада не нужен вершинный буфер
    vertex_input_info.pVertexBindingDescriptions = nullptr;
    vertex_input_info.vertexAttributeDescriptionCount = 0;
    vertex_input_info.pVertexAttributeDescriptions = nullptr;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // Квад - это два треугольника
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)this->vulkan_context->get_swapchain_extent().width;
    viewport.height = (float)this->vulkan_context->get_swapchain_extent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = this->vulkan_context->get_swapchain_extent();

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = &viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // Или VK_FRONT_FACE_COUNTER_CLOCKWISE, в зависимости от вершин квада
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable = VK_FALSE;
    color_blending.logicOp = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount = 1;
    color_blending.pAttachments = &color_blend_attachment;
    color_blending.blendConstants[0] = 0.0f;
    color_blending.blendConstants[1] = 0.0f;
    color_blending.blendConstants[2] = 0.0f;
    color_blending.blendConstants[3] = 0.0f;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input_info;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisampling;
    pipeline_info.pColorBlendState = &color_blending;
    pipeline_info.layout = pipeline_layout_;
    pipeline_info.renderPass = this->vulkan_context->get_render_pass();
    pipeline_info.subpass = 0;
    pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

    MY_VK_CHECK(vkCreateGraphicsPipelines(this->vulkan_context->get_device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline_));

    vkDestroyShaderModule(this->vulkan_context->get_device(), frag_shader_module, nullptr);
    vkDestroyShaderModule(this->vulkan_context->get_device(), vert_shader_module, nullptr);
}

void CpuRenderer::destroy_vulkan_display_resources() {
    if (!this->vulkan_context) {
        return;
    }

    VkDevice device = this->vulkan_context->get_device();
    if (device == VK_NULL_HANDLE) return;

    if (graphics_pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device, graphics_pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, pipeline_layout_, nullptr);
    if (descriptor_pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
    if (descriptor_set_layout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, descriptor_set_layout_, nullptr);
    if (cpu_texture_sampler_ != VK_NULL_HANDLE) vkDestroySampler(device, cpu_texture_sampler_, nullptr);
    if (cpu_texture_image_view_ != VK_NULL_HANDLE) vkDestroyImageView(device, cpu_texture_image_view_, nullptr);
    if (cpu_texture_image_ != VK_NULL_HANDLE) vkDestroyImage(device, cpu_texture_image_, nullptr);
    if (cpu_texture_image_memory_ != VK_NULL_HANDLE) vkFreeMemory(device, cpu_texture_image_memory_, nullptr);
    if (staging_buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device, staging_buffer_, nullptr);
    if (staging_buffer_memory_ != VK_NULL_HANDLE) vkFreeMemory(device, staging_buffer_memory_, nullptr);

    graphics_pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    descriptor_set_layout_ = VK_NULL_HANDLE;
    cpu_texture_sampler_ = VK_NULL_HANDLE;
    cpu_texture_image_view_ = VK_NULL_HANDLE;
    cpu_texture_image_ = VK_NULL_HANDLE;
    cpu_texture_image_memory_ = VK_NULL_HANDLE;
    staging_buffer_ = VK_NULL_HANDLE;
    staging_buffer_memory_ = VK_NULL_HANDLE;
}

void CpuRenderer::perform_cpu_rendering_pass(const Camera& camera, const Mesh& mesh) {
    clear_framebuffer(0xFF000000); // Очищаем буфер кадра, например, черным цветом

    LiteMath::float4x4 model_matrix = LiteMath::translate4x4(LiteMath::float3(0.0f, 0.0f, 0.0f));
    LiteMath::float4x4 view_matrix = camera.get_view_matrix();
    LiteMath::float4x4 proj_matrix = camera.get_projection_matrix();
    LiteMath::float4x4 mvp = proj_matrix * view_matrix * model_matrix;

    const auto& vertices = mesh.get_vertices();
    const auto& indices = mesh.get_indices();

    for (size_t i = 0; i < indices.size(); i += 3) {
        const Vertex& v0_raw = vertices[indices[i + 0]];
        const Vertex& v1_raw = vertices[indices[i + 1]];
        const Vertex& v2_raw = vertices[indices[i + 2]];

        LiteMath::float4 p0 = mvp * LiteMath::to_float4(v0_raw.position, 1.0f);
        LiteMath::float4 p1 = mvp * LiteMath::to_float4(v1_raw.position, 1.0f);
        LiteMath::float4 p2 = mvp * LiteMath::to_float4(v2_raw.position, 1.0f);

        if (p0.w == 0.0f || p1.w == 0.0f || p2.w == 0.0f) continue;
        p0 = p0 / p0.w;
        p1 = p1 / p1.w;
        p2 = p2 / p2.w;

        float half_width = this->width / 2.0f;
        float half_height = this->height / 2.0f;

        LiteMath::float4 v0_screen = LiteMath::float4(
            p0.x * half_width + half_width,
            p0.y * -half_height + half_height,
            p0.z,
            p0.w
        );
        LiteMath::float4 v1_screen = LiteMath::float4(
            p1.x * half_width + half_width,
            p1.y * -half_height + half_height,
            p1.z,
            p1.w
        );
        LiteMath::float4 v2_screen = LiteMath::float4(
            p2.x * half_width + half_width,
            p2.y * -half_height + half_height,
            p2.z,
            p2.w
        );

        draw_triangle(v0_screen, v1_screen, v2_screen, v0_raw.color, v1_raw.color, v2_raw.color);
    }
}

void CpuRenderer::save_framebuffer_to_file(const std::string& filename) {
    LiteImage::Image2D<uint32_t> image_to_save(width, height, framebuffer.data());
    bool success = LiteImage::SaveImage<uint32_t>(filename.c_str(), image_to_save);

    if (success) {
        std::cout << "Framebuffer successfully saved to: " << filename << std::endl;
    } else {
        std::cerr << "Failed to save framebuffer to: " << filename << ". Check file extension and library support (e.g., STB_IMAGE)." << std::endl;
    }
}

void CpuRenderer::display_rendered_frame_with_vulkan() {
    if (!this->vulkan_context) {
        throw std::runtime_error("[display_rendered_frame_with_vulkan]: vulkan context not inited");
    }

    void* data;
    MY_VK_CHECK(vkMapMemory(this->vulkan_context->get_device(), staging_buffer_memory_, 0, this->width * this->height * sizeof(uint32_t), 0, &data));
    memcpy(data, this->framebuffer.data(), this->width * this->height * sizeof(uint32_t));
    vkUnmapMemory(this->vulkan_context->get_device(), staging_buffer_memory_);

    VkCommandBuffer transfer_cmd_buf = vk_utils::createCommandBuffer(this->vulkan_context->get_device(), this->vulkan_context->get_command_pool());
    this->vulkan_context->transition_image_layout(transfer_cmd_buf, cpu_texture_image_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    this->vulkan_context->copy_buffer_to_image(transfer_cmd_buf, staging_buffer_, cpu_texture_image_, this->width, this->height);
    this->vulkan_context->transition_image_layout(transfer_cmd_buf, cpu_texture_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vk_utils::executeCommandBufferNow(transfer_cmd_buf, this->vulkan_context->get_graphics_queue(), this->vulkan_context->get_device());

    VkCommandBuffer cmd_buf = this->vulkan_context->begin_frame();
    if (cmd_buf == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline_);
    vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1, &descriptor_set_, 0, nullptr);
    vkCmdDraw(cmd_buf, 6, 1, 0, 0);

    this->vulkan_context->end_frame(cmd_buf);
}

void CpuRenderer::render(const Camera& camera, const Mesh& mesh) {
    perform_cpu_rendering_pass(camera, mesh);
    display_rendered_frame_with_vulkan();
}

void CpuRenderer::render(const Camera& a_camera, const Mesh& a_mesh, const std::string& a_filename) {
    perform_cpu_rendering_pass(a_camera, a_mesh);
    save_framebuffer_to_file(a_filename);
}

void CpuRenderer::render(const Camera& a_camera, const SdfGrid& a_sdf_grid, const std::string& a_filename) {
    perform_cpu_sdf_rendering_pass(a_camera, a_sdf_grid);
    save_framebuffer_to_file(a_filename);
}

void CpuRenderer::clear_framebuffer(uint32_t a_color) {
    std::fill(this->framebuffer.begin(), this->framebuffer.end(), a_color);
}

void CpuRenderer::set_pixel(int a_x, int a_y, uint32_t a_color) {
    if (a_x >= 0 && a_x < this->width && a_y >= 0 && a_y < this->height) {
        this->framebuffer[a_y * this->width + a_x] = a_color;
    }
}

uint32_t CpuRenderer::pack_color(const LiteMath::float3& a_color) {
    uint32_t r_byte = static_cast<uint32_t>(std::clamp(a_color.x, 0.0f, 1.0f) * 255.0f);
    uint32_t g_byte = static_cast<uint32_t>(std::clamp(a_color.y, 0.0f, 1.0f) * 255.0f);
    uint32_t b_byte = static_cast<uint32_t>(std::clamp(a_color.z, 0.0f, 1.0f) * 255.0f);
    uint32_t a_byte = 255;

    return (a_byte << 24) | (b_byte << 16) | (g_byte << 8) | r_byte; // VK_FORMAT_R8G8B8A8_UNORM Little-Endian
}

// Базовая растеризация треугольника с интерполяцией цвета по барицентрическим координатам
void CpuRenderer::draw_triangle(const LiteMath::float4& v0_screen, const LiteMath::float4& v1_screen, const LiteMath::float4& v2_screen,
                                const LiteMath::float3& c0_orig, const LiteMath::float3& c1_orig, const LiteMath::float3& c2_orig) {
    // Область для растеризации:
    int x_min = static_cast<int>(std::floor(std::min({v0_screen.x, v1_screen.x, v2_screen.x})));
    int x_max = static_cast<int>(std::ceil(std::max({v0_screen.x, v1_screen.x, v2_screen.x})));
    int y_min = static_cast<int>(std::floor(std::min({v0_screen.y, v1_screen.y, v2_screen.y})));
    int y_max = static_cast<int>(std::ceil(std::max({v0_screen.y, v1_screen.y, v2_screen.y})));

    // Обрезаем до границ экрана
    x_min = std::max(0, x_min);
    x_max = std::min(this->width - 1, x_max);
    y_min = std::max(0, y_min);
    y_max = std::min(this->height - 1, y_max);

    // Вычисляем площадь треугольника (для знаменателя барицентрических координат)
    float area_denom = (v1_screen.y - v2_screen.y) * (v0_screen.x - v2_screen.x) + (v2_screen.x - v1_screen.x) * (v0_screen.y - v2_screen.y);
    if (std::abs(area_denom) < 1e-6f) return; // Вырожденный треугольник

    for (int y = y_min; y <= y_max; ++y) {
        for (int x = x_min; x <= x_max; ++x) {
            LiteMath::float3 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 0.0f); // Семплируем в центре пикселя

            // Вычисляем барицентрические координаты
            float lambda0 = ((v1_screen.y - v2_screen.y) * (p.x - v2_screen.x) + (v2_screen.x - v1_screen.x) * (p.y - v2_screen.y)) / area_denom;
            float lambda1 = ((v2_screen.y - v0_screen.y) * (p.x - v0_screen.x) + (v0_screen.x - v2_screen.x) * (p.y - v0_screen.y)) / area_denom;
            float lambda2 = 1.0f - lambda0 - lambda1;

            // Проверяем, находится ли точка внутри треугольника
            if (lambda0 >= 0 && lambda1 >= 0 && lambda2 >= 0) {
                // Интерполируем цвет
                LiteMath::float3 interpolated_color = lambda0 * c0_orig + lambda1 * c1_orig + lambda2 * c2_orig;
                set_pixel(x, y, pack_color(interpolated_color));
            }
        }
    }
}

void CpuRenderer::perform_cpu_sdf_rendering_pass(const Camera& camera, const SdfGrid& a_sdf_grid) {
    clear_framebuffer(0xFF000000);

    // Пример использования sample_sdf_trilinear:
    // LiteMath::float3 test_point = LiteMath::float3(0.1f, 0.1f, 0.1f);
    // float sdf_value = sample_sdf_trilinear(test_point);
    // std::cout << "SDF at (" << test_point.x << ", " << test_point.y << ", " << test_point.z << "): " << sdf_value << std::endl;

    LiteMath::float3 camera_pos = camera.get_position();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            LiteMath::float3 world_coord = LiteMath::float3(
                (static_cast<float>(x) / width - 0.5f) * 3.0f,
                (static_cast<float>(y) / height - 0.5f) * 3.0f,
                0.0f
            );
            float sdf_val = a_sdf_grid.sample(world_coord);

            LiteMath::float3 color;
            if (sdf_val < 0.0f) {
                color = LiteMath::float3(std::clamp(-sdf_val * 2.0f, 0.0f, 1.0f), 0.0f, 0.0f);
            } else {
                color = LiteMath::float3(0.0f, 0.0f, std::clamp(sdf_val * 2.0f, 0.0f, 1.0f));
            }
            set_pixel(x, y, pack_color(color));
        }
    }
}

