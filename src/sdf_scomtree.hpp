#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

#include "scenes/scomtree/scomtree.hpp"

#include "shader_common.hpp"

namespace sdf_raster {

void load_scomtree (SComTree& scene, const std::filesystem::path& path);
float sample_sdf (const SComTree& scene, const LiteMath::float3& p);

struct SComTreeTreeDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer header_buffer = VK_NULL_HANDLE;
    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    VkBuffer bricks_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_modifiers_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_add_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

SComTreeTreeDescriptorSetInfo create_sdf_scomtree_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const SComTree& scomtree
        , size_t subtree_root_level
        , size_t max_frames_in_flight);

void cleanup_sdf_scomtree_descriptor_set (VkDevice device, SComTreeTreeDescriptorSetInfo& info);

std::vector <NodeContext> get_scomtree_subtrees_payloads (const SComTree& scene, int max_level_to_descend);
std::vector <NodeContext> get_scomtree_subtrees_payloads_parallel (const SComTree& scene, int max_level_to_descend);
int get_scomtree_max_depth (const SComTree& scene);

}

