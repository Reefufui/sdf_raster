#pragma once

#include "renderer.hpp"
#include "vulkan_context.hpp"
#include <vector>
#include <string>
#include <array>
#include <stdexcept>

struct GLFWwindow;

namespace sdf_raster {

struct Settings;
class SceneManager;

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

void init (std::shared_ptr <VulkanContext> vulkan_context, std::shared_ptr <SceneManager> scene_manager, const InitInfo& info, Settings& settings);

void update (Settings& settings, const Stats& stats);

void draw (uint32_t image_index, VkCommandBuffer cmd_buff);

void cleanup (Settings& settings);

}

}

