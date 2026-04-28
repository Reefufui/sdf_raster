// vulkan/presentation/presentation_context.hpp
#pragma once

#include "../context/vulkan_context.hpp"
#include "vk_images.h"
#include "vk_swapchain.h"

#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace sdf_raster {

class PresentationContext {
public:
    PresentationContext (std::shared_ptr <VulkanContext> a_context, GLFWwindow* a_window);
    ~PresentationContext ();

    void shutdown ();

    inline bool is_initialized () const { return this->initialized; }

    inline VkExtent2D get_swapchain_extent () const { return this->swapchain.GetExtent (); }
    inline VkFormat get_swapchain_image_format () const { return this->swapchain.GetFormat (); }
    inline uint32_t get_swapchain_image_count () const { return this->swapchain.GetImageCount (); }
    inline std::vector <VkImageView> get_swapchain_image_views () {
        std::vector <VkImageView> views (this->swapchain.GetImageCount ());
        for (size_t i = 0; i < views.size (); i++) {
            views [i] = this->swapchain.GetAttachment (static_cast <uint32_t> (i)).view;
        }
        return views;
    }

    VkCommandBuffer begin_frame (uint32_t a_frame_index);
    void end_frame (VkCommandBuffer a_cmd_buff, uint32_t a_frame_index);
    inline uint32_t get_total_frames () const { return this->max_frames_in_flight; }
    inline uint32_t get_swapchain_image_index () const { return this->acquired_image_index; }

    inline VkRenderPass get_render_pass () const { return this->main.render_pass; }
    inline VkRenderPass get_render_pass_after () const { return this->after.render_pass; }
    inline VkFramebuffer get_swapchain_framebuffer () const {
        return this->main.framebuffer [this->acquired_image_index];
    }
    inline VkFramebuffer get_swapchain_framebuffer_after () const {
        return this->after.framebuffer [this->acquired_image_index];
    }
    inline const vk_utils::VulkanImageMem& get_depth_buffer () const {
        return this->depth_buffers [this->acquired_image_index];
    }
    inline VkFormat get_depth_format () const { return this->depth_format; }

    inline void register_resizable (std::function <void ()> a_func) {
        this->resizable_callbacks.push_back (a_func);
    }
    inline void set_resized_flag () { this->framebuffer_resized = true; }

    inline VkQueue get_present_queue () const { return this->present_queue; }

private:
    void create_surface ();
    void create_swapchain (uint32_t a_width, uint32_t a_height);
    VkRenderPass create_render_pass (VkAttachmentLoadOp a_load_op);
    void create_depth_buffers ();
    std::vector <VkFramebuffer> create_framebuffers (VkRenderPass a_render_pass);
    void create_frame_resources ();

    void destroy_swapchain ();
    void destroy_depth_buffers ();
    void destroy_framebuffers ();
    void destroy_frame_resources ();

    void resize ();

    std::shared_ptr <VulkanContext> context = nullptr;
    GLFWwindow* window = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VulkanSwapChain swapchain;
    VkQueue present_queue = VK_NULL_HANDLE;
    std::vector <vk_utils::VulkanImageMem> depth_buffers;
    VkFormat depth_format;
    std::vector <VkSemaphore> gpu_ready_to_present;
    size_t frames_in_swapchain = 3;
    uint32_t acquired_image_index = 0;
    bool framebuffer_resized = false;

    struct FrameResources {
        VkFence cpu_wait_next_frame = VK_NULL_HANDLE;
        VkSemaphore wait_before_color_attachment_output = VK_NULL_HANDLE;
        VkSemaphore wait_before_depth_copy = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    };
    std::vector <FrameResources> frame_resources;
    const size_t max_frames_in_flight = 2;

    struct RenderPassResources {
        std::vector <VkFramebuffer> framebuffer;
        VkRenderPass render_pass = VK_NULL_HANDLE;
    };
    RenderPassResources main;
    RenderPassResources after;

    std::vector <std::function <void ()>> resizable_callbacks;

    bool initialized = false;
};

} // namespace sdf_raster
