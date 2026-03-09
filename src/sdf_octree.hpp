#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

#include "shader_common.hpp"

namespace sdf_raster {

struct SdfOctree {
    std::string name;
    std::vector <SdfOctreeNode> nodes;
};

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

struct ActiveLeafsDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> active_leaf_counter_buffers;
    std::vector <VkBuffer> active_leafs_buffers;
    std::vector <VkBuffer> active_leaf_vertices_count_buffers;
    std::vector <VkBuffer> active_leaf_indices_count_buffers;

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

ActiveLeafsDescriptorSetInfo create_active_leafs_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , size_t active_leafs_count
        , size_t max_frames_in_flight);

DrawIndexedIndirectCommandDescriptorSetInfo create_draw_indexed_indirect_command_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , size_t max_frames_in_flight);

void cleanup_sdf_octree_descriptor_set (VkDevice device, SdfOctreeDescriptorSetInfo& info);
void cleanup_active_leafs_descriptor_set (VkDevice device, ActiveLeafsDescriptorSetInfo& info);
void cleanup_draw_indexed_indirect_command_descriptor_set (VkDevice device, DrawIndexedIndirectCommandDescriptorSetInfo& info);

std::vector <NodeContext> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend);
std::vector <NodeContext> get_octree_subtrees_payloads_parallel (const SdfOctree& scene, int max_level_to_descend);
int get_octree_max_depth (const SdfOctree& scene);

std::vector <NodeContext> fetch_active_leafs (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame);
uint32_t fetch_active_leaf_counter (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t frame);
std::vector <uint> fetch_vertices_count (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame);
std::vector <uint> fetch_indices_count (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame);

}

