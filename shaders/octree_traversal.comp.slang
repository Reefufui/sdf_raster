// octree_traversal.comp.slang

#include "shader_common.hpp"

#include "frustum_culling.slang"
#include "occlusion_culling.slang"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;

[[vk::binding (0, 1)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 1)]] RWStructuredBuffer <NodeContext> g_active_leafs;

[[vk::binding (0, 2)]] ConstantBuffer <FrustumGeometry> g_frustum_geometry;

[[vk::binding (0, 3)]] Sampler2D <float> g_depth_sampler;

[[vk::binding (0, 4)]] RWStructuredBuffer <LevelOfDetail> g_lod;

void process_voxel (NodeContext context, SdfOctreeNode leaf_node) {
    context.cube_index = 0;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        context.cube_index |= (leaf_node.values [i] < 0.0f ? (1 << i) : 0);
    }

    if (context.cube_index == 0 || context.cube_index == 255) {
        return;
    }

    uint active_leaf_offset = 0;
    InterlockedAdd (g_active_leaf_counter [0], 1, active_leaf_offset);

    if (active_leaf_offset >= pc.active_leafs_max_count) {
        return;
    }

    g_active_leafs [active_leaf_offset] = context;
}

bool is_visible_node (int level, float3 node_coord, float node_size, out float max_dim) {
    if (level <= pc.frustum_culling_level) {
        if (!aabb_cube_frustum_intersects (g_frustum_geometry, node_coord, node_size)) {
            max_dim = 0.f;
            return false;
        }
    }

    if (level <= pc.occlusion_culling_level) {
        float3 child_corners [8];
        [unroll]
        for (int i = 0; i < 8; ++i) {
            child_corners [i].x = node_coord.x + (((i >> 0) & 1) * node_size);
            child_corners [i].y = node_coord.y + (((i >> 1) & 1) * node_size);
            child_corners [i].z = node_coord.z + (((i >> 2) & 1) * node_size);
        }

        if (voxel_occluded (g_depth_sampler, pc.prev_view_proj, child_corners, max_dim)) {
            return false;
        }
    }

    return true;
}

#define MAX_OCTREE_DEPTH 16
#define MAX_DIRECT_DESCEND_PATH 5

// NOTE: root expected to have children
void dfs_octree (NodeContext root, uint lod) {
    int node_index_stack [MAX_OCTREE_DEPTH - MAX_DIRECT_DESCEND_PATH];
    float3 coord_stack [MAX_OCTREE_DEPTH - MAX_DIRECT_DESCEND_PATH];
    int path_stack [MAX_OCTREE_DEPTH - MAX_DIRECT_DESCEND_PATH];

    node_index_stack [0] = root.node_index;
    coord_stack [0] = float3 (root.min_corner_x, root.min_corner_y, root.min_corner_z);
    path_stack [1] = 0;

    const int dfs_root_level = pc.subtree_root_level + MAX_DIRECT_DESCEND_PATH;
    int dfs_level = 1; // starting with dfs_level 1
    while (dfs_level > 0) {
        int child_index = path_stack [dfs_level];
        if (child_index == 8) { // no more children
            dfs_level -= 1;
            continue;
        }
        path_stack [dfs_level] += 1;

        SdfOctreeNode parent_node = g_octree_data [node_index_stack [dfs_level - 1]];
        int child_node_index = parent_node.offset + child_index;
        SdfOctreeNode child_node = g_octree_data [child_node_index];

        float child_voxel_size = root.voxel_size / (float)(1u << dfs_level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        child_corner_offset.x = (float)((child_index >> 0) & 1) * child_voxel_size;
        child_corner_offset.y = (float)((child_index >> 1) & 1) * child_voxel_size;
        child_corner_offset.z = (float)((child_index >> 2) & 1) * child_voxel_size;

        float3 child_coord = coord_stack [dfs_level - 1] + child_corner_offset;

        float max_dim;
        if (!is_visible_node (dfs_root_level + dfs_level, child_coord, child_voxel_size, max_dim)) {
            continue;
        }

        if (child_node.offset == 0 || dfs_root_level + dfs_level == lod) { // leaf
            NodeContext context;
            context.voxel_size = child_voxel_size;
            context.min_corner_x = child_coord.x;
            context.min_corner_y = child_coord.y;
            context.min_corner_z = child_coord.z;
            context.node_index = child_node_index;
            process_voxel (context, child_node);
        } else { // internal node
            node_index_stack [dfs_level] = child_node_index;
            coord_stack [dfs_level] = child_coord;

            dfs_level += 1;
            path_stack [dfs_level] = 0;
        }
    }
}

bool descend_path (uint path [MAX_DIRECT_DESCEND_PATH], inout NodeContext current_context, uint lod, out bool force_process_voxel) {
    force_process_voxel = true;
    const int direct_root_level = pc.subtree_root_level;

    [unroll]
    for (uint direct_level = 0; direct_level < MAX_DIRECT_DESCEND_PATH; ++direct_level) {
        uint child_index = path [direct_level];
        SdfOctreeNode parent_node = g_octree_data [current_context.node_index];

        if (parent_node.offset == 0 || direct_root_level + direct_level == lod) {
            [unroll]
            for (; direct_level < MAX_DIRECT_DESCEND_PATH; ++direct_level) {
                if (path [direct_level] != 0u) {
                    return false;
                }
            }
            return true;
        }

        const float node_size = current_context.voxel_size / 2.f;

        current_context.voxel_size = node_size;
        current_context.min_corner_x += (((child_index >> 0) & 1) * node_size);
        current_context.min_corner_y += (((child_index >> 1) & 1) * node_size);
        current_context.min_corner_z += (((child_index >> 2) & 1) * node_size);
        current_context.node_index = parent_node.offset + child_index;

        float3 node_coord = float3 (current_context.min_corner_x, current_context.min_corner_y, current_context.min_corner_z);
        float max_dim;
        if (!is_visible_node (direct_root_level + direct_level, node_coord, node_size, max_dim)) {
            return false;
        }
    }

    force_process_voxel = direct_root_level + MAX_DIRECT_DESCEND_PATH == lod;
    return true;
}

uint get_lod (bool store) {
    const float min_distance = pc.near_plane;
    const float max_distance = pc.far_plane;
    const uint min_lod = pc.subtree_root_level;
    const uint max_lod = pc.max_lod;

    const float distance = distance (float3 (0, 0, 0), pc.camera_pos.xyz);

    if (distance < min_distance) {
        return max_lod;
    } else if (distance > max_distance) {
        return min_lod;
    }

    const float normalized_distance = (distance - min_distance) / (max_distance - min_distance);
    const float current_lod_float = float (min_lod) + ((1.f - normalized_distance) * (float)(max_lod - min_lod));
    const uint lod = uint (round (current_lod_float));

    if (store) {
        g_lod [0].lod = lod;
        g_lod [0].distance = distance;
    }

    return lod;
}

[shader ("compute")]
[numthreads (8, 8, 8)]
void main (uint3 dti :  SV_DispatchThreadID, uint3 gi : SV_GroupID, uint3 gti : SV_GroupThreadID) {
    NodeContext current_context = g_roots [gi.z];
    const uint path [MAX_DIRECT_DESCEND_PATH] = {gi.y, gi.x, gti.z, gti.y, gti.x};
    bool force_process_voxel = false;
    uint lod = get_lod (dti.x == 0 && dti.y == 0 && dti.z == 0);

    if (descend_path (path, current_context, lod, force_process_voxel)) {
        SdfOctreeNode node = g_octree_data [current_context.node_index];

        if (node.offset == 0 || force_process_voxel) {
            process_voxel (current_context, node);
        } else {
            dfs_octree (current_context, lod);
        }
    }

    return;
}

