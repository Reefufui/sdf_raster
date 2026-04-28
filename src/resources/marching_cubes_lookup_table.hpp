// resources/marching_cubes_lookup_table.hpp

#pragma once

#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_utils.h>

#include <memory>

#include <LiteMath.h>
#include "data/marching_cubes_data.hpp"

namespace sdf_raster {

class MarchingCubesLookupTableDescriptorSetInfo {
public:
    MarchingCubesLookupTableDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags);

    VkDescriptorSet get_descriptor_set (uint32_t /*fif_index*/) const { return this->descriptor_set; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    ~MarchingCubesLookupTableDescriptorSetInfo ();

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

	VkBuffer edge_corners_buffer = VK_NULL_HANDLE;
	VkBuffer cube_index_2_edge_mask_buffer = VK_NULL_HANDLE;
	VkBuffer cube_index_2_triangle_indices_buffer = VK_NULL_HANDLE;
	VkBuffer cube_index_2_mesh_output_counts_buffer = VK_NULL_HANDLE;

	VkDeviceMemory device_memory = VK_NULL_HANDLE;
};

} // sdf_raster