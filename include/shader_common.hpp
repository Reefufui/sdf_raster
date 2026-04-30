// shader_common.h

#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#define VOXELS_PER_COMPUTE_WORKGROUP 256
#define VOXELS_PER_MESH_WORKGROUP 16 // AMD
#define BRICKS_PER_COMPUTE_WORKGROUP 32

#ifdef __cplusplus

#include <array>
#include <LiteMath.h>

using uint2 = LiteMath::uint2;
using float3 = LiteMath::float3;
using float4 = LiteMath::float4;
using float4x4 = LiteMath::float4x4;
using uint = unsigned int; // TODO: check if uint32_t works

#else

struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

#endif // __cplusplus

static const uint MAX_LEAF_VERTS = 12;
static const uint MAX_LEAF_PRIMS = 4;
static const uint MAX_BRICK_VERTS = 96;
static const uint MAX_BRICK_PRIMS = 32;

struct IndirectDispatch {
    uint x;
    uint y;
    uint z;
};

struct Vertex {
    float4 position;
    float4 normal;
    float4 color;
};

struct NodeContext {
    float min_corner_x;
    float min_corner_y;
    float min_corner_z;
    float voxel_size;
    int node_index;
    int cube_index;
};

struct TaskPayload {
    uint leaf_buffer_offset;
    uint leaf_count;
    uint vertices_count;
    uint triangles_count;
};

#ifdef __cplusplus
#define column_major
#else
#define alignas(x)
#endif

struct PushConstantsData {
    alignas (16) column_major float4x4 view_proj;
    alignas (16) column_major float4x4 prev_view_proj;
    alignas (16) float4 camera_pos;
    alignas (4)  float far_plane;
    alignas (4)  float near_plane;
    alignas (4)  uint max_octree_depth;
    alignas (4)  uint max_lod;
    alignas (4)  uint subtree_root_level;
    alignas (4)  uint active_leafs_max_count;
    alignas (4)  uint occlusion_culling_level;
    alignas (4)  uint frustum_culling_level;
    alignas (4)  uint color_leafs;
    alignas (4)  uint lod_mode;  // 0 = global, 1 = per-node
    alignas (16) float4 root_center;
    alignas (4)  float lod_threshold_pixels;
    alignas (4)  float fov_y;
    alignas (4)  uint screen_width;
    alignas (4)  uint screen_height;
    alignas (4)  float min_voxel_size;
    alignas (4)  float max_voxel_size;
};

struct DeferredLightingPushConstants {
    float4 camera_pos;         // xyz = camera world pos
    float4 light_pos;          // xyz = light world pos
    float4 light_color;        // rgb = color, a unused

    float4 fog_color;          // rgb = fog color, a unused

    float  ambient_strength;   // 0.1 default
    float  specular_strength;  // 0.4 default
    float  shininess;          // 64.0 default
    float  depth_threshold;    // 0.0001 default

    float  fog_start;          // 0.999 default
    float  fog_end;            // 1.0 default

    uint   enable_hz_write;    // 0 default
};

struct SComTreeHeader {
    uint brick_size;
    uint v_size;
    uint bits_per_value;
    uint values_per_uint;
    uint value_mask;
    uint bitmask_len;
    uint dimension;

    uint child_rot_shift;
    uint child_rot_mask;
    uint child_add_shift;
    uint child_add_mask;
    uint child_offset_mask;
    uint child_offset_off;
    uint node_offset_mask;
    uint uints_per_link;
    uint unique_brick_prefix;
    uint unique_brick_offset_mask;

    uint children_types_shift;
    uint children_types_mask;
    uint base_reference_shift;
    uint children_active_bits_shift;
    uint children_active_bits_mask;
    uint references_offset;
    uint reference_bits;
    uint reference_mask;
    uint references_per_uint;
    uint links_offset;
    uint max_surface_count;
    uint max_surface_count_per_leaf;

    uint bricks_step;
    uint bricks_arr_offset;
    uint nodes_arr_offset;
    uint root_node_off;

    uint has_channels;
    uint has_surfaces;
    uint has_multi_nodes;

    int tex_id_off;
    int mat_id_off;
    int all_float_tex_id_off;
    int all_int_mat_id_off;

    float max_val;
    uint max_depth;
    float user_params [7];

    //to allow further extensions without breaking binary compatibility
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint _pad3;
    uint _pad4;
};

struct SComTreeStackElement {
    alignas (4) uint links_offset;
    alignas (4) uint transform;
    alignas (4) uint info;
    alignas (8) uint2 p_size;
};

struct SComTreeBrickPayload {
    alignas (8) uint2 p_size; // 16bit : x | 16bit : y | 16bit: z | 16bit : size
    alignas (4) uint rot_link; // 6bit : rotation | 26bit: link
};

struct SdfOctreeNode {
    float values [8]; // NOTE: for each internal node
    uint offset; // NOTE: offset for children (they are stored together). 0 offset means it's a leaf
};

struct FrustumGeometry {
    float4 vertices [8];
    float4 normals [6];
};

struct LevelOfDetail {
    alignas (4) uint max_lod;
    alignas (4) uint min_lod;
    alignas (4) uint lod;
    alignas (4) float distance;
};

#endif // SHADER_COMMON_H

