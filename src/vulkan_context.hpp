#pragma once

#include "vk_utils.h"
#include "vk_context.h"
#include "GLFW/glfw3.h"

#include <memory>
#include <vector>

struct GLFWwindow;

class VulkanContext : public vk_utils::VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    void init(GLFWwindow* window, int width, int height);
    void shutdown();
    void resize(int width, int height);

    VkDevice get_device() const { return this->device; }
    VkQueue get_graphics_queue() const { return this->computeQueue; }
    VkRenderPass get_render_pass() const { return this->render_pass; }
    VkExtent2D get_swapchain_extent() const { return this->swap_chain_extent; }
    VkFormat get_swapchain_image_format() const { return this->swap_chain_image_format; }

    VkCommandBuffer begin_frame();
    void end_frame(VkCommandBuffer command_buffer);

    void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
    void create_image_and_view(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& image_memory, VkImageView& image_view);
    void transition_image_layout(VkCommandBuffer cmd_buf, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
    void copy_buffer_to_image(VkCommandBuffer cmd_buf, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);
    VkCommandPool get_command_pool() const { return this->commandPool; }


private:
    void setup_debug_messenger();
    void create_swap_chain(int width, int height);
    void create_image_views();
    void create_render_pass();
    void create_framebuffers();
    void create_command_buffers();
    void create_sync_objects();

    VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;

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

