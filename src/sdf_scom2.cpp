#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

#include "vk_buffers.h"

#include "logger.hpp"
#include "sdf_scom2.hpp"
#include "scenes/scom2/defs.hpp"

namespace sdf_raster {

float sample_sdf (const SCom2Tree& /*scene*/, const LiteMath::float3& /*p*/) {
    // TODO
    assert (false);
    return 0.f;
}

SCom2TreeDescriptorSetInfo create_sdf_scom2_descriptor_set (
    VkDevice /*device*/
    , VkPhysicalDevice /*physical_device*/
    , std::shared_ptr <vk_utils::ICopyEngine> /*copy_helper*/
    , vk_utils::DescriptorMaker& /*ds_maker*/
    , VkShaderStageFlags /*shader_stage_flags*/
    , const SCom2Tree& /*scom2*/
    , const size_t /*subtree_root_level*/
    , size_t /*max_frames_in_flight*/) {
    SCom2TreeDescriptorSetInfo info = {};
    return info;
}

void cleanup_sdf_scom2_descriptor_set (VkDevice device, SCom2TreeDescriptorSetInfo& info) {
    if (info.nodes_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.nodes_buffer, nullptr);
        info.nodes_buffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < info.subtree_root_buffers.size (); ++i) {
        if (info.subtree_root_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.subtree_root_buffers [i], nullptr);
            info.subtree_root_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

std::vector <NodeContext> get_scom2_subtrees_payloads (const SCom2Tree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

std::vector <NodeContext> get_scom2_subtrees_payloads_parallel (const SCom2Tree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

int get_scom2_max_depth (const SCom2Tree& /*scene*/) {
    // TODO
    assert (false);
    return 0;
}

}

