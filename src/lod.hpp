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

class LODDescriptorSetInfo {
public:
    LODDescriptorSetInfo (VkDevice device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkPhysicalDevice physical_device
        , VkShaderStageFlags shader_stage_flags
        , size_t count
        , size_t max_frames_in_flight);
    ~LODDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    LevelOfDetail fetch_lod (size_t frame, size_t index);

private:
    VkDevice device = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine> copy_helper;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> lod_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

}

