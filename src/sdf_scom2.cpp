#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

#include "vk_buffers.h"

#include "logger.hpp"
#include "sdf_scom2.hpp"
#include "sdf/scom2/defs.hpp"

namespace sdf_raster {

void load_scom2 (scom2::SCom2Tree& scene, const std::filesystem::path& path) {
    std::ifstream fs (path, std::ios::binary);

    uint32_t magic_number = 0;
    uint32_t version = 0;
    uint32_t num_nodes = 0;
    uint32_t num_bricks = 0;
    uint32_t vc_count = 0;
    uint32_t pc_count = 0;

    fs.read ((char *)&magic_number, sizeof (uint32_t));

    if (magic_number != scom2::SCOM2_MAGIC_NUMBER) {
        fs.close ();
        LOG_ERROR ("Legacy scom2 is not supported.");
        return;
    }

    fs.read ((char *)&version, sizeof (uint32_t));

    if (version != scom2::SCOM2_VERSION) {
        fs.close ();
        printf ("[ERROR] SCom2 version mismatch (save is version %u, current version is %u)\n", version, scom2::SCOM2_VERSION);
        return;
    }

    fs.read ((char *)&num_nodes, sizeof (uint32_t));
    fs.read ((char *)&num_bricks, sizeof (uint32_t));
    fs.read ((char *)&vc_count, sizeof (uint32_t));
    fs.read ((char *)&pc_count, sizeof (uint32_t));
    fs.read ((char *)&scene.header, sizeof (scom2::Header));

    scene.nodes.resize (num_nodes);
    scene.bricks.resize (num_bricks);

    fs.read ((char *)scene.nodes.data (), num_nodes * sizeof (uint32_t));
    fs.read ((char *)scene.bricks.data (), num_bricks * sizeof (uint32_t));

    scene.voxel_channels.resize (vc_count);
    scene.point_channels.resize (pc_count);

    for (auto &ch : scene.voxel_channels) {
        load_data_channel (fs, ch);
    }

    for (auto &ch : scene.point_channels) {
        load_data_channel (fs, ch);
    }

    fs.close ();
    scene.name = path.stem ().string ();
}

float sample_sdf (const scom2::SCom2Tree& /*scene*/, const LiteMath::float3& /*p*/) {
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
    , const scom2::SCom2Tree& /*scom2*/
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

std::vector <NodeContext> get_scom2_subtrees_payloads (const scom2::SCom2Tree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

std::vector <NodeContext> get_scom2_subtrees_payloads_parallel (const scom2::SCom2Tree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

int get_scom2_max_depth (const scom2::SCom2Tree& /*scene*/) {
    // TODO
    assert (false);
    return 0;
}

}

