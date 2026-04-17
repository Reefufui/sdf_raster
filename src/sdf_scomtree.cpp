#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

#include "vk_buffers.h"

#include "logger.hpp"
#include "sdf_scomtree.hpp"
#include "scenes/scomtree/defs.hpp"

namespace sdf_raster {

float sample_sdf (const SComTree& /*scene*/, const LiteMath::float3& /*p*/) {
    // TODO
    assert (false);
    return 0.f;
}

SComTreeTreeDescriptorSetInfo create_sdf_scomtree_descriptor_set (
    VkDevice /*device*/
    , VkPhysicalDevice /*physical_device*/
    , std::shared_ptr <vk_utils::ICopyEngine> /*copy_helper*/
    , vk_utils::DescriptorMaker& /*ds_maker*/
    , VkShaderStageFlags /*shader_stage_flags*/
    , const SComTree& /*scomtree*/
    , const size_t /*subtree_root_level*/
    , size_t /*max_frames_in_flight*/) {
    SComTreeTreeDescriptorSetInfo info = {};
    return info;
}

void cleanup_sdf_scomtree_descriptor_set (VkDevice device, SComTreeTreeDescriptorSetInfo& info) {
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

std::vector <NodeContext> get_scomtree_subtrees_payloads (const SComTree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

std::vector <NodeContext> get_scomtree_subtrees_payloads_parallel (const SComTree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

int get_scomtree_max_depth (const SComTree& /*scene*/) {
    // TODO
    assert (false);
    return 0;
}

}

