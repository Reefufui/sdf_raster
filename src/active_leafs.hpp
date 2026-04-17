#pragma once

#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_utils.h>

#include <memory>
#include <vector>

namespace sdf_raster {

struct ActiveLeafsDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> active_leaf_counter_buffers;
    std::vector <VkBuffer> active_leafs_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

ActiveLeafsDescriptorSetInfo create_active_leafs_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkDeviceSize active_leafs_size
        , size_t max_frames_in_flight);

void cleanup_active_leafs_descriptor_set (VkDevice device, ActiveLeafsDescriptorSetInfo& info);

uint32_t fetch_active_leaf_counter (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t frame);

}

