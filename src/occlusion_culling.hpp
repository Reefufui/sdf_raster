#pragma once

#include <memory>
#include <vector>

#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_images.h"

namespace sdf_raster {

struct DepthBufferDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
};

DepthBufferDescriptorSetInfo create_depth_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const std::vector <vk_utils::VulkanImageMem>& depth_textures
        , VkSampler detph_sampler
        , size_t max_frames_in_flight);

}

