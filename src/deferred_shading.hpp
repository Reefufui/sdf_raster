#pragma once

#include <vk_descriptor_sets.h> // DescriptorMaker
#include <vk_images.h> // VulkanImageMem
#include <vk_include.h> // vk* Vk*

#include <vector>

struct DeferredShadingConfig {
    VkExtent2D extent;
    std::vector <VkFormat> gbuffer_formats;
    VkFormat depth_format;
    VkFormat swapchain_format;
    uint32_t num_inflight_frames;
    VkFilter filter = VK_FILTER_LINEAR;
};

class DeferredShading {
public:
    DeferredShading (VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue
        , const DeferredShadingConfig& config, const std::vector <VkImageView>& swapchain_views);
    ~DeferredShading ();

    DeferredShading (const DeferredShading&) = delete;
    DeferredShading& operator= (const DeferredShading&) = delete;

    DeferredShading (DeferredShading&&) noexcept;
    DeferredShading& operator= (DeferredShading&&) noexcept;

    VkRenderPass get_gbuffer_pass () const { return this->gbuffer_pass; }
    VkRenderPass get_lighting_pass () const { return this->lighting_pass; }
    VkFramebuffer get_gbuffer_fb (uint32_t fif_index) const { return this->gbuffer_cascades [fif_index].framebuffer; }
    VkFramebuffer get_lighting_fb (uint32_t swap_index) const { return this->lighting_framebuffers [swap_index]; }
    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    VkImage get_depth_buffer (uint32_t fif_index) const { return this->gbuffer_cascades [fif_index].images [3].image; }

private:
    VkDevice device = VK_NULL_HANDLE;

    VkRenderPass gbuffer_pass = VK_NULL_HANDLE;
    VkRenderPass lighting_pass = VK_NULL_HANDLE;

    struct GBufferCascade {
        std::vector <vk_utils::VulkanImageMem> images; // NOTE: Pos, Norm, Albedo, Depth
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    std::vector <GBufferCascade> gbuffer_cascades; // NOTE: In-Flight count
    std::vector <VkFramebuffer> lighting_framebuffers; // NOTE: Swapchain count

    VkSampler sampler = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets; // NOTE: In-Flight count
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
};

