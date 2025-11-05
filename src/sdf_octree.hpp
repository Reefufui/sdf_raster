#pragma once

#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"
#include "shaders/common.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

namespace sdf_raster {

struct SdfOctree {
  std::vector <SdfOctreeNode> nodes;
};

void load_sdf_octree (SdfOctree &scene, const std::string &path);
void save_sdf_octree (const SdfOctree &scene, const std::string &path);
void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump);
float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p);

struct SdfOctreeDescriptorSetInfo {
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

  VkBuffer nodes_buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

SdfOctreeDescriptorSetInfo create_sdf_octree_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , const sdf_raster::SdfOctree& octree
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags);

void cleanup_sdf_octree_descriptor_set (VkDevice device, SdfOctreeDescriptorSetInfo& info);

}

