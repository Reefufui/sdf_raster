#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "shader_common.hpp"

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"

namespace sdf_raster {

    struct LODDescriptorSetInfo {
        std::vector <VkDescriptorSet> descriptor_sets;
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

        std::vector <VkBuffer> lod_buffers;

        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    LODDescriptorSetInfo create_lod_descriptor_set (
            VkDevice device
            , VkPhysicalDevice physical_device
            , vk_utils::DescriptorMaker& ds_maker
            , VkShaderStageFlags shader_stage_flags
            , size_t count
            , size_t max_frames_in_flight);

    void cleanup_lod_descriptor_set (VkDevice device, LODDescriptorSetInfo& info);

    LevelOfDetail fetch_lod (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, LODDescriptorSetInfo info, size_t frame, size_t index);

}

