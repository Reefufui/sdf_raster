#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"

#include "renderer.hpp"
#include "vulkan_context.hpp"


class CpuRenderer : public IRenderer {
public:
    CpuRenderer(std::shared_ptr<VulkanContext> vulkan_context);
    ~CpuRenderer() override;

    void init(int width, int height, GLFWwindow* window) override;
    void render(const Camera& camera, const Mesh& mesh) override;
    void resize(int width, int height) override;
    void shutdown() override;

private:
    void create_cpu_render_target(int width, int height);
    void destroy_cpu_render_target();
    void create_vulkan_display_resources();
    void destroy_vulkan_display_resources();

    void clear_framebuffer(uint32_t color);
    void draw_triangle(const LiteMath::float4& v0_screen, const LiteMath::float4& v1_screen, const LiteMath::float4& v2_screen,
                       const LiteMath::float3& c0, const LiteMath::float3& c1, const LiteMath::float3& c2);

    void set_pixel(int x, int y, uint32_t color);
    uint32_t pack_color(const LiteMath::float3& color);

private:
    std::shared_ptr<VulkanContext> vulkan_context;
    int width = 0;
    int height = 0;
    std::vector<uint32_t> framebuffer; // RGBA

    VkBuffer staging_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory cpu_texture_image_memory_ = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory_ = VK_NULL_HANDLE;
    VkImage cpu_texture_image_ = VK_NULL_HANDLE;
    VkImageView cpu_texture_image_view_ = VK_NULL_HANDLE;
    VkSampler cpu_texture_sampler_ = VK_NULL_HANDLE;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkPipeline graphics_pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};

