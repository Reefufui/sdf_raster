// resources/frustum.hpp
// gpu_resource_bundles/frustum.hpp

#pragma once

#include "shader_common.hpp"

#include <vk_descriptor_sets.h>
#include <vk_copy.h>

#include <memory>

namespace sdf_raster {

class FrustumDescriptorSetInfo {
public:
    FrustumDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , VkShaderStageFlags shader_stage_flags
        , size_t max_frames_in_flight);
    ~FrustumDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    void* get_frustum_geometry_memory_ptr (uint32_t fif_index) const { return this->frustum_geometry_memories_mapped [fif_index]; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine> copy_helper;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> frustum_geometry_buffers;
    std::vector <VkDeviceMemory> frustum_geometry_memories;
    std::vector <void*> frustum_geometry_memories_mapped;
};

} // sdf_raster

