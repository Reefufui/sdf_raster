// resources/active_leafs.hpp

#pragma once

#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_utils.h>

#include <memory>
#include <vector>

namespace sdf_raster {

class ActiveLeafsDescriptorSetInfo {
public:
    ActiveLeafsDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , VkDeviceSize active_leafs_size
        , size_t max_frames_in_flight);
    ~ActiveLeafsDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    VkBuffer get_active_leaf_counter_buffer (uint32_t fif_index) const { return this->active_leaf_counter_buffers [fif_index]; }
    uint32_t fetch_active_leaf_counter (uint32_t fif_index);

private:
    VkDevice device = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine> copy_helper;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> active_leaf_counter_buffers;
    std::vector <VkBuffer> active_leafs_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

} // sdf_raster