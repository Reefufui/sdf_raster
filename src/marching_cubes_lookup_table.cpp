#include <vector>
#include <stdexcept>
#include <algorithm>
#include <memory>

#include "marching_cubes_lookup_table.hpp"
#include "vk_buffers.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

namespace sdf_raster {

MarchingCubesLookupTableDescriptorSetInfo create_lookup_table_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr<vk_utils::ICopyEngine> copy_helper
    , vk_utils::DescriptorMaker& desc_maker
    , VkShaderStageFlags shader_stage_flags) {
  MarchingCubesLookupTableDescriptorSetInfo info = {};

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

  info.edge_corners_buffer = buffers [0];
  info.cube_index_2_edge_mask_buffer = buffers [1];
  info.cube_index_2_triangle_indices_buffer = buffers [2];
  info.cube_index_2_mesh_output_counts_buffer = buffers [3];

  info.device_memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

  copy_helper->UpdateBuffer (info.edge_corners_buffer, 0, edge_corners, edge_corners_size);
  copy_helper->UpdateBuffer (info.cube_index_2_edge_mask_buffer, 0, cube_index_2_edge_mask, cubeIndex2EdgeMaskSize);
  copy_helper->UpdateBuffer (info.cube_index_2_triangle_indices_buffer, 0, cube_index_2_triangle_indices, cubeIndex2TriangleIndicesSize);
  copy_helper->UpdateBuffer (info.cube_index_2_mesh_output_counts_buffer, 0, cube_index_2_mesh_output_counts, cubeIndex2MeshOutputCountsSize);

  desc_maker.BindBegin (shader_stage_flags);
  desc_maker.BindBuffer (0, info.edge_corners_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  desc_maker.BindBuffer (1, info.cube_index_2_edge_mask_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  desc_maker.BindBuffer (2, info.cube_index_2_triangle_indices_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  desc_maker.BindBuffer (3, info.cube_index_2_mesh_output_counts_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  desc_maker.BindEnd (&info.descriptor_set, &info.descriptor_set_layout);

  return info;
}

void cleanup_lookup_table_descriptor_set (VkDevice device, MarchingCubesLookupTableDescriptorSetInfo& info) {
  vkDestroyBuffer (device, info.edge_corners_buffer, nullptr);
  vkDestroyBuffer (device, info.cube_index_2_edge_mask_buffer, nullptr);
  vkDestroyBuffer (device, info.cube_index_2_triangle_indices_buffer, nullptr);
  vkDestroyBuffer (device, info.cube_index_2_mesh_output_counts_buffer, nullptr);

  vkFreeMemory(device, info.device_memory, nullptr);

  info = {};
}

} // namespace sdf_raster

