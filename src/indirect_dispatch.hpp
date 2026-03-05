#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vk_descriptor_sets.h"

namespace sdf_raster {

    struct IndirectDispatchDescriptorSetInfo {
        std::vector <VkDescriptorSet> descriptor_sets;
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

        std::vector <VkBuffer> indirect_dispatch_buffers;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    IndirectDispatchDescriptorSetInfo create_indirect_dispatch_descriptor_set (
            VkDevice device
            , VkPhysicalDevice physical_device
            , vk_utils::DescriptorMaker& ds_maker
            , VkShaderStageFlags shader_stage_flags
            , size_t max_frames_in_flight);

    void cleanup_indirect_dispatch_descriptor_set (VkDevice device, IndirectDispatchDescriptorSetInfo& info);
}

