#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

#include "scenes/scom2/scom2.hpp"

#include "shader_common.hpp"

namespace sdf_raster {

void load_scom2 (scom2::SCom2Tree& scene, const std::filesystem::path& path);
float sample_sdf (const scom2::SCom2Tree& scene, const LiteMath::float3& p);

struct SCom2TreeDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

SCom2TreeDescriptorSetInfo create_sdf_scom2_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const scom2::SCom2Tree& scom2
        , size_t subtree_root_level
        , size_t max_frames_in_flight);

void cleanup_sdf_scom2_descriptor_set (VkDevice device, SCom2TreeDescriptorSetInfo& info);

std::vector <NodeContext> get_scom2_subtrees_payloads (const scom2::SCom2Tree& scene, int max_level_to_descend);
std::vector <NodeContext> get_scom2_subtrees_payloads_parallel (const scom2::SCom2Tree& scene, int max_level_to_descend);
int get_scom2_max_depth (const scom2::SCom2Tree& scene);

}

