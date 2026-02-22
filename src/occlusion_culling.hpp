#pragma once

#include <memory>
#include <vector>

#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_images.h"

namespace sdf_raster {

struct DepthBufferDescriptorSetInfo {
    VkDescriptorSet descriptor_set;

    vk_utils::VulkanImageMem hz_buffer;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
};

DepthBufferDescriptorSetInfo create_depth_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent);

}

