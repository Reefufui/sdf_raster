#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_utils.h"

#include "common/data_channel.hpp"

#include "shader_common.hpp"

namespace sdf_raster {

static constexpr unsigned SCOM2_CHILD_EMPTY        = 0;
static constexpr unsigned SCOM2_CHILD_LEAF_VOLUME  = 1;
static constexpr unsigned SCOM2_CHILD_LEAF_SURFACE = 3;
static constexpr unsigned SCOM2_CHILD_NODE         = 2;

static constexpr unsigned SCOM2_CHILD_TYPE_BITS    = 2;
static constexpr unsigned SCOM2_CHILD_TYPE_MASK    = (1 << SCOM2_CHILD_TYPE_BITS) - 1;

static constexpr unsigned SCOM2_MAGIC_NUMBER = 0xffffdefa;
static constexpr unsigned SCOM2_VERSION = 4;

struct SCom2Header {
    uint32_t brick_size;
    uint32_t v_size;
    uint32_t bits_per_value;
    uint32_t values_per_uint;
    uint32_t value_mask;
    uint32_t bitmask_len;
    uint32_t dimension;

    uint32_t child_rot_shift;
    uint32_t child_rot_mask;
    uint32_t child_add_shift;
    uint32_t child_add_mask;
    uint32_t child_offset_mask;
    uint32_t child_offset_off;
    uint32_t node_offset_mask;
    uint32_t uints_per_link;
    uint32_t unique_brick_prefix;
    uint32_t unique_brick_offset_mask;

    uint32_t children_types_shift;
    uint32_t children_types_mask;
    uint32_t base_reference_shift;
    uint32_t children_active_bits_shift;
    uint32_t children_active_bits_mask;
    uint32_t references_offset;
    uint32_t reference_bits;
    uint32_t reference_mask;
    uint32_t references_per_uint;
    uint32_t links_offset;
    uint32_t max_surface_count;
    uint32_t max_surface_count_per_leaf;

    uint32_t bricks_step;
    uint32_t bricks_arr_offset;
    uint32_t nodes_arr_offset;
    uint32_t root_node_off;

    uint32_t has_channels;
    uint32_t has_surfaces;
    uint32_t has_multi_nodes;

    int tex_id_off;
    int mat_id_off;
    int all_float_tex_id_off;
    int all_int_mat_id_off;

    float max_val;
    uint32_t max_depth;
    float user_params [7];

    //to allow further extensions without breaking binary compatibility
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
    uint32_t _pad3;
    uint32_t _pad4;
};

struct SCom2Tree {
    std::string name;

    SCom2Header header;
    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;

    std::vector <DataChannel> point_channels;
    std::vector <DataChannel> voxel_channels;
};

void load_scom2 (SCom2Tree& scene, const std::filesystem::path& path);
void dump_sdf_scom2_text (const SCom2Tree &scene, const std::string &path_to_dump);
float sample_sdf (const SCom2Tree& scene, const LiteMath::float3& p);

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
        , const sdf_raster::SCom2Tree& scom2
        , size_t subtree_root_level
        , size_t max_frames_in_flight);

void cleanup_sdf_scom2_descriptor_set (VkDevice device, SCom2TreeDescriptorSetInfo& info);

std::vector <NodeContext> get_scom2_subtrees_payloads (const SCom2Tree& scene, int max_level_to_descend);
std::vector <NodeContext> get_scom2_subtrees_payloads_parallel (const SCom2Tree& scene, int max_level_to_descend);
int get_scom2_max_depth (const SCom2Tree& scene);

}

