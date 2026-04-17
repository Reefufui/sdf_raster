#pragma once

#include "scenes/octree/octree.hpp"

#include "shader_common.hpp"

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_utils.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sdf_raster {

void load_sdf_octree (SdfOctree& scene, const std::filesystem::path& path);
void save_sdf_octree (const SdfOctree &scene, const std::string &path);
void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump);
float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p);

struct SdfOctreeDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct DrawIndexedIndirectCommandDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> draw_indexed_indirect_command_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

SdfOctreeDescriptorSetInfo create_sdf_octree_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const sdf_raster::SdfOctree& octree
        , size_t subtree_root_level
        , size_t max_frames_in_flight);

DrawIndexedIndirectCommandDescriptorSetInfo create_draw_indexed_indirect_command_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , size_t max_frames_in_flight);

void cleanup_sdf_octree_descriptor_set (VkDevice device, SdfOctreeDescriptorSetInfo& info);
void cleanup_draw_indexed_indirect_command_descriptor_set (VkDevice device, DrawIndexedIndirectCommandDescriptorSetInfo& info);

std::vector <NodeContext> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend);
std::vector <NodeContext> get_octree_subtrees_payloads_parallel (const SdfOctree& scene, int max_level_to_descend);
int get_octree_max_depth (const SdfOctree& scene);

}

