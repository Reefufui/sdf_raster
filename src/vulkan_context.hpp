#pragma once

#include "vk_utils.h"
#include "vk_context.h"
#include "GLFW/glfw3.h"

#include <memory>
#include <vector>

struct GLFWwindow;

namespace sdf_raster {

class VulkanContext {
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

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

    inline VkExtent2D get_swapchain_extent () const { return this->swap_chain_extent; }
    inline VkFormat get_swapchain_image_format () const { return this->swap_chain_image_format; }
    inline VkRenderPass get_render_pass () const { return this->render_pass; }

    VkCommandBuffer begin_frame ();
    void end_frame (VkCommandBuffer command_buffer);

private:
    void create_instance ();
    void setup_debug_utils_messenger ();
    void create_device ();
    void create_command_pools ();
    void get_device_queues ();

    void create_main_command_buffers ();
    void create_framebuffers ();
    void create_image_views ();
    void create_render_pass ();
    void create_swap_chain (int width, int height);
    void create_sync_objects ();

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
    std::shared_ptr <vk_utils::ICopyEngine> copy_helper = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    GLFWwindow* window = nullptr;

    VkSwapchainKHR swap_chain = VK_NULL_HANDLE;
    std::vector<VkImage> swap_chain_images;
    std::vector<VkImageView> swap_chain_image_views;
    VkFormat swap_chain_image_format;
    VkExtent2D swap_chain_extent;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swap_chain_framebuffers;
    std::vector<VkCommandBuffer> command_buffers;

    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkSemaphore> image_available_semaphores_acquire;
    std::vector<VkFence> in_flight_fences;
    uint32_t current_frame = 0;

    bool initialized = false;
};

}

