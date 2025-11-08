#pragma once

#include "vk_context.h"
#include "vk_swapchain.h"
#include "vk_utils.h"
#include "GLFW/glfw3.h"

#include <memory>
#include <vector>

struct GLFWwindow;

namespace sdf_raster {

class VulkanContext {
public:
    void init (int a_width, int a_height);
    void init (GLFWwindow* window, int width, int height);
    void shutdown ();
    void resize (int width, int height);

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
    inline VkRenderPass get_render_pass () const { return this->render_pass; }

    VkCommandBuffer begin_frame ();
    void end_frame (VkCommandBuffer command_buffer);

private:
    void create_instance ();
    void setup_debug_utils_messenger ();
    void create_device ();
    void create_command_pools ();
    void get_device_queues ();
    void create_render_pass ();
    void create_frame_resources ();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
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
    std::vector <VkFramebuffer> swapchain_framebuffers;
    VkRenderPass render_pass = VK_NULL_HANDLE;

    const int max_frames_in_flight = 3;
    uint32_t current_frame = 0;
    uint32_t current_image_index;
    struct FrameResources {
        VkSemaphore ready_to_present;
        VkSemaphore ready_to_render;
        VkFence ready_to_record;
        VkCommandBuffer command_buffer;
    };
    std::vector <FrameResources> frame_resources;

    bool initialized = false;
};

}

