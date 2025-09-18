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
    VulkanContext();
    ~VulkanContext();

    // init, shutdown, resize
    void init(GLFWwindow* window, int width, int height);
    void shutdown();
    void resize(int width, int height);

    // getters
    inline VkCommandPool get_compute_command_pool_reset () const { return this->compute_command_pool_reset; }
    inline VkCommandPool get_compute_command_pool_transistent () const { return this->compute_command_pool_transistent; }
    inline VkCommandPool get_graphics_command_pool_reset () const { return this->graphics_command_pool_reset; }
    inline VkCommandPool get_graphics_command_pool_transistent () const { return this->graphics_command_pool_transistent; }
    inline VkCommandPool get_transfer_command_pool_reset () const { return this->transfer_command_pool_reset; }
    inline VkCommandPool get_transfer_command_pool_transistent () const { return this->transfer_command_pool_transistent; }
    inline VkDevice get_device () const { return this->device; }
    inline VkExtent2D get_swapchain_extent () const { return this->swap_chain_extent; }
    inline VkFormat get_swapchain_image_format () const { return this->swap_chain_image_format; }
    inline VkInstance get_instance () const { return this->instance; }
    inline VkPhysicalDevice get_physical_device () const { return this->physical_device; }
    inline VkQueue get_graphics_queue () const { return this->compute_queue; }
    inline VkRenderPass get_render_pass () const { return this->render_pass; }

    // command buffer management
    VkCommandBuffer begin_frame();
    void end_frame(VkCommandBuffer command_buffer);

    // resource management
    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
    void create_image_and_view(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory, VkImageView& image_view);
    void transition_image_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkCommandBuffer cmd_buf, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

private:
    void create_instance ();
    void create_device ();
    void setup_debug_utils_messenger ();

    void create_command_buffers();
    void create_framebuffers();
    void create_image_views();
    void create_render_pass();
    void create_swap_chain(int width, int height);
    void create_sync_objects();

private:
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    vk_utils::QueueFID_T queue_ids {};
    VkCommandPool compute_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool compute_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_transistent = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
    VkQueue compute_queue = VK_NULL_HANDLE;
    VkQueue transfer_queue = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine>  pCopyHelper       = nullptr;
    std::shared_ptr <vk_utils::IMemoryAlloc> pAllocatorCommon  = nullptr;
    std::shared_ptr <vk_utils::IMemoryAlloc> pAllocatorSpecial = nullptr;

    VkPhysicalDeviceSubgroupProperties subgroupProps;

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

    // Объекты синхронизации
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    uint32_t current_frame = 0;

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
};

}

