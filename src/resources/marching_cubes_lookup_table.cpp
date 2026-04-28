// resources/marching_cubes_lookup_table.cpp
#include "marching_cubes_lookup_table.hpp"
#include "data/marching_cubes_data.hpp"
#include "resources/marching_cubes_lookup_table.hpp"

#include <vk_buffers.h>
#include <vk_utils.h>

namespace sdf_raster {

MarchingCubesLookupTableDescriptorSetInfo::MarchingCubesLookupTableDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , VkShaderStageFlags shader_stage_flags) : device (device) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    VkDeviceSize edge_corners_size = sizeof (LiteMath::uint2) * 12;
    VkDeviceSize cubeIndex2EdgeMaskSize = sizeof (int) * 256;
    VkDeviceSize cubeIndex2TriangleIndicesSize = sizeof (int) * 256 * 16;
    VkDeviceSize cubeIndex2MeshOutputCountsSize = sizeof (LiteMath::uint2) * 256;

    std::vector <VkBuffer> buffers (4);
    std::vector <VkMemoryRequirements> mem_reqs (4);

    buffers [0] = vk_utils::createBuffer (device, edge_corners_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    buffers [1] = vk_utils::createBuffer (device, cubeIndex2EdgeMaskSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [1]);
    buffers [2] = vk_utils::createBuffer (device, cubeIndex2TriangleIndicesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2]);
    buffers [3] = vk_utils::createBuffer (device, cubeIndex2MeshOutputCountsSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [3]);

    this->edge_corners_buffer = buffers [0];
    this->cube_index_2_edge_mask_buffer = buffers [1];
    this->cube_index_2_triangle_indices_buffer = buffers [2];
    this->cube_index_2_mesh_output_counts_buffer = buffers [3];

    this->device_memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (this->edge_corners_buffer, 0, edge_corners, edge_corners_size);
    copy_helper->UpdateBuffer (this->cube_index_2_edge_mask_buffer, 0, cube_index_2_edge_mask, cubeIndex2EdgeMaskSize);
    copy_helper->UpdateBuffer (this->cube_index_2_triangle_indices_buffer, 0, cube_index_2_triangle_indices, cubeIndex2TriangleIndicesSize);
    copy_helper->UpdateBuffer (this->cube_index_2_mesh_output_counts_buffer, 0, cube_index_2_mesh_output_counts, cubeIndex2MeshOutputCountsSize);


    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, 1);

    desc_maker->BindBegin (shader_stage_flags);
    desc_maker->BindBuffer (0, this->edge_corners_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    desc_maker->BindBuffer (1, this->cube_index_2_edge_mask_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    desc_maker->BindBuffer (2, this->cube_index_2_triangle_indices_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    desc_maker->BindBuffer (3, this->cube_index_2_mesh_output_counts_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    desc_maker->BindEnd (&this->descriptor_set, &this->descriptor_set_layout);
}

MarchingCubesLookupTableDescriptorSetInfo::~MarchingCubesLookupTableDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    vkDestroyBuffer (this->device, this->edge_corners_buffer, nullptr);
    vkDestroyBuffer (this->device, this->cube_index_2_edge_mask_buffer, nullptr);
    vkDestroyBuffer (this->device, this->cube_index_2_triangle_indices_buffer, nullptr);
    vkDestroyBuffer (this->device, this->cube_index_2_mesh_output_counts_buffer, nullptr);

    vkFreeMemory (this->device, this->device_memory, nullptr);
}

} // namespace sdf_raster