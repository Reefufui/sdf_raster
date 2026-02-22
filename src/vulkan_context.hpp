#pragma once

#include "vk_context.h"
#include "vk_images.h"
#include "vk_swapchain.h"
#include "vk_utils.h"
#include "GLFW/glfw3.h"

#include <memory>
#include <vector>

struct GLFWwindow;

namespace sdf_raster {

class VulkanContext {
public:
    void init (int a_width, int a_height, bool a_mesh_shader_support);
    void init (GLFWwindow* window, int width, int height, bool a_mesh_shader_support);
    void shutdown ();

    // getters
    inline bool is_initialized () const { return this->initialized; };
    inline VkInstance get_instance () const { return this->instance; }
    inline VkPhysicalDevice get_physical_device () const { return this->physical_device; }
    inline VkDevice get_device () const { return this->device; }
    inline VkCommandPool get_compute_command_pool_reset () const { return this->compute_command_pool_reset; }
    inline VkCommandPool get_compute_command_pool_transistent () const { return this->compute_command_pool_transistent; }
    inline VkCommandPool get_graphics_command_pool_reset () const { return this->graphics_command_pool_reset; }
    inline VkCommandPool get_graphics_command_pool_transistent () const { return this->graphics_command_pool_transistent; }
    inline VkCommandPool get_transfer_command_pool_reset () const { return this->transfer_command_pool_reset; }
    inline VkCommandPool get_transfer_command_pool_transistent () const { return this->transfer_command_pool_transistent; }
    inline VkQueue get_compute_queue () const { return this->compute_queue; }
    inline VkQueue get_graphics_queue () const { return this->graphics_queue; }
    inline VkQueue get_transfer_queue () const { return this->transfer_queue; }
    inline std::shared_ptr <vk_utils::ICopyEngine> get_copy_helper () const { return this->copy_helper; }

    inline VkExtent2D get_swapchain_extent () const { return this->swapchain.GetExtent (); }
    inline VkFormat get_swapchain_image_format () const { return this->swapchain.GetFormat (); }
    inline VkRenderPass get_render_pass () const { return this->main.render_pass; }
    inline VkRenderPass get_render_pass_after () const { return this->after.render_pass; }
    inline VkFramebuffer get_swapchain_framebuffer () const { return this->main.framebuffer [this->acquired_image_index]; }
    inline VkFramebuffer get_swapchain_framebuffer_after () const { return this->after.framebuffer [this->acquired_image_index]; }
    inline const vk_utils::VulkanImageMem& get_depth_buffer () const { return this->depth_buffer; }

    VkCommandBuffer begin_frame ();
    void end_frame (VkCommandBuffer command_buffer);
    uint32_t get_current_frame () { return this->current_frame; }
    uint32_t get_total_frames () { return this->max_frames_in_flight; }

private:
    void create_instance ();
    void dump_mesh_shader_properties () const;
    void create_device (bool a_mesh_shader_support);
    void create_command_pools ();
    void get_device_queues ();
    VkRenderPass create_render_pass (VkAttachmentLoadOp load_op);
    void create_frame_resources ();
    void create_depth_buffer ();
    void destroy_depth_buffer ();
    void destroy_framebuffers ();
    void destroy_frame_resources ();

    void resize (int width, int height);
    std::vector <VkFramebuffer> create_framebuffers (VkRenderPass a_render_pass);

#ifdef VULKAN_VALIDATION_LAYERS
    void setup_debug_utils_messenger ();
    VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
#endif

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    vk_utils::QueueFID_T device_queue_ids {};
    VkCommandPool compute_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool compute_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_transistent = VK_NULL_HANDLE;
    VkQueue compute_queue = VK_NULL_HANDLE;
    VkQueue transfer_queue = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    VkQueue present_queue = VK_NULL_HANDLE;
    std::shared_ptr <vk_utils::ICopyEngine> copy_helper = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    GLFWwindow* window = nullptr;

    VulkanSwapChain swapchain;

    vk_utils::VulkanImageMem depth_buffer;

    struct RenderPassResources {
        std::vector <VkFramebuffer> framebuffer;
        VkRenderPass render_pass = VK_NULL_HANDLE;
    };

    RenderPassResources main;
    RenderPassResources after;

    uint32_t acquired_image_index;

    const size_t max_frames_in_swapchain = 5;
    const size_t max_frames_in_flight = 3;
    uint32_t current_frame = 2;
    struct FrameResources {
        VkSemaphore ready_to_present;
        VkSemaphore ready_to_render;
        VkFence ready_to_record;
        VkCommandBuffer command_buffer;
    };
    std::vector <FrameResources> frame_resources;

    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties;

    bool initialized = false;
};

}

