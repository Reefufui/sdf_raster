// engine/deferred_shading.hpp
#pragma once

#include "shader_common.hpp" // DeferredLightingPushConstants
#include "vulkan/presentation/presentation_context.hpp"

#include <vk_descriptor_sets.h> // DescriptorMaker
#include <vk_images.h> // VulkanImageMem
#include <vk_include.h> // vk* Vk*

#include <vector>

struct DeferredShadingConfig {
    std::vector <VkFormat> gbuffer_formats;
    VkFilter filter = VK_FILTER_LINEAR;
};

class DeferredShading {
public:
    DeferredShading (VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue
        , const DeferredShadingConfig& config, std::shared_ptr <sdf_raster::PresentationContext> a_presentation);
    ~DeferredShading ();

    DeferredShading (const DeferredShading&) = delete;
    DeferredShading& operator= (const DeferredShading&) = delete;

    DeferredShading (DeferredShading&&) noexcept;
    DeferredShading& operator= (DeferredShading&&) noexcept;

    VkRenderPass get_gbuffer_pass () const { return this->gbuffer_pass; }
    VkRenderPass get_lighting_pass () const { return this->lighting_pass; }
    VkRenderPass get_render_pass_after () const { return this->after_pass; }
    VkFramebuffer get_gbuffer_fb () const { return this->g_buffer_framebuffer; }
    VkFramebuffer get_lighting_fb (uint32_t swap_index) const { return this->lighting_framebuffers [swap_index]; }
    VkFramebuffer get_framebuffer_after (uint32_t swap_index) const { return this->after_framebuffers [swap_index]; }
    VkDescriptorSet get_descriptor_set () const { return this->descriptor_set; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }
    DeferredLightingPushConstants& push_constants_ref () { return this->push_constants; }

    VkImage get_depth_buffer () const {
        assert (this->g_buffer.back ().aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT && "last image of g_buffer must be depth buffer");
        return this->g_buffer.back ().image;
    }

private:
    VkDevice device = VK_NULL_HANDLE;

    VkRenderPass gbuffer_pass = VK_NULL_HANDLE;
    VkRenderPass lighting_pass = VK_NULL_HANDLE;
    VkRenderPass after_pass = VK_NULL_HANDLE;

    std::vector <vk_utils::VulkanImageMem> g_buffer; // NOTE: Pos, Norm, Albedo, Depth
    VkFramebuffer g_buffer_framebuffer = VK_NULL_HANDLE;

    std::vector <VkFramebuffer> lighting_framebuffers; // NOTE: Swapchain count
    std::vector <VkFramebuffer> after_framebuffers; // NOTE: In-Flight count

    VkSampler sampler = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    VkDescriptorSet descriptor_set;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    DeferredLightingPushConstants push_constants;
    std::shared_ptr <sdf_raster::PresentationContext> presentation_context;
};

