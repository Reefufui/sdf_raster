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

void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump);
float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p);

class SdfOctreeDescriptorSetInfo {
public:
    SdfOctreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , const sdf_raster::SdfOctree& octree
        , size_t subtree_root_level
        , size_t max_frames_in_flight);
    ~SdfOctreeDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    VkBuffer get_subtree_root_buffer (uint32_t fif_index) const { return this->subtree_root_buffers [fif_index]; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// TODO: move to scenes/octree
std::vector <NodeContext> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend);
std::vector <NodeContext> get_octree_subtrees_payloads_parallel (const SdfOctree& scene, int max_level_to_descend);
int get_octree_max_depth (const SdfOctree& scene);

}

