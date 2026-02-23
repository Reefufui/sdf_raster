#pragma once

#include "vulkan_context.hpp"
#include <vector>
#include <string>
#include <array>
#include <stdexcept>

struct GLFWwindow;

namespace sdf_raster {

namespace gui {

struct InitInfo {
    VkDevice device;
    GLFWwindow* window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkQueue graphics_queue;
    uint32_t graphics_queue_family_index;
    std::vector <VkImageView> swapchain_image_views;
    VkExtent2D surface_extent;
    VkFormat surface_format;
    VkFormat depth_format;
};

void init (const InitInfo& info);

/**
     * @brief Updates the ImGui state for a new frame.
     *        This includes setting display size, delta time, and starting a new ImGui frame.
     *        Call this once per application frame before any ImGui::Begin() calls.
     * @param width Current width of the display/framebuffer.
     * @param height Current height of the display/framebuffer.
     * @param delta_time Time elapsed since the last frame, in seconds.
     */
void update (uint32_t width, uint32_t height, float delta_time);

/**
     * @brief Records ImGui draw commands into the provided Vulkan command buffer.
     *        This should be called within your main render loop, after your scene rendering.
     * @param image_index The index of the current swapchain image/framebuffer to draw to.
     * @param cmd_buff The Vulkan command buffer to record drawing commands into.
     */
void draw (uint32_t image_index, VkCommandBuffer cmd_buff);

void cleanup ();

}

}

