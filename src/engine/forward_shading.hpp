// engine/forward_shading.hpp
#pragma once

#include "vulkan/presentation/render_target.hpp"

#include <vk_images.h>
#include <vk_include.h>

#include <memory>
#include <vector>

namespace sdf_raster {

class ForwardShading {
public:
    ForwardShading (VkDevice a_device, VkPhysicalDevice a_physical_device, std::shared_ptr <RenderTarget> a_render_target);
    ~ForwardShading ();

    ForwardShading (const ForwardShading&) = delete;
    ForwardShading& operator= (const ForwardShading&) = delete;

    ForwardShading (ForwardShading&&) noexcept;
    ForwardShading& operator= (ForwardShading&&) noexcept;

    VkRenderPass get_render_pass () const { return this->main.render_pass; }
    VkRenderPass get_render_pass_after () const { return this->after.render_pass; }
    VkFramebuffer get_framebuffer (uint32_t a_image_index) const { return this->main.framebuffer [a_image_index]; }
    VkFramebuffer get_framebuffer_after (uint32_t a_image_index) const { return this->after.framebuffer [a_image_index]; }
    VkImage get_depth_image () const { return this->depth_buffer.image; }
    VkImageView get_depth_view () const { return this->depth_buffer.view; }
    VkFormat get_depth_format () const { return this->depth_format; }

    void recreate_framebuffers (VkPhysicalDevice a_physical_device);

private:
    void create_render_passes ();
    void create_depth_buffer (VkPhysicalDevice a_physical_device);
    void create_framebuffers ();
    void destroy_render_passes ();
    void destroy_depth_buffer ();
    void destroy_framebuffers ();

    VkRenderPass create_render_pass (VkAttachmentLoadOp load_op);

    std::shared_ptr <RenderTarget> render_target;
    VkDevice device = VK_NULL_HANDLE;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    vk_utils::VulkanImageMem depth_buffer;

    struct RenderPassResources {
        std::vector <VkFramebuffer> framebuffer;
        VkRenderPass render_pass = VK_NULL_HANDLE;
    };
    RenderPassResources main;
    RenderPassResources after;
};

} // namespace sdf_raster
