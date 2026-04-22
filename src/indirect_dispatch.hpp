#pragma once

#include <vk_descriptor_sets.h>

#include <memory>
#include <vector>

namespace sdf_raster {

class IndirectDescriptorSetInfo {
public:
    IndirectDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , VkShaderStageFlags shader_stage_flags
        , VkDeviceSize indirect_dispatch_size
        , size_t max_frames_in_flight);
    ~IndirectDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const {
        return (fif_index + 1 > this->descriptor_sets.size ()) ? VK_NULL_HANDLE : this->descriptor_sets [fif_index];
    }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    VkBuffer get_indirect_buffer (size_t fif_index) {
        return (fif_index + 1 > this->indirect_dispatch_buffers.size ()) ? this->indirect_dispatch_buffers [0] : this->indirect_dispatch_buffers [fif_index];
    }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> indirect_dispatch_buffers;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

}

