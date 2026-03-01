// octree_traversal.comp.slang

#include "shader_common.hpp"

#include "frustum_culling.slang"
#include "occlusion_culling.slang"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
[[vk::binding (2, 0)]] StructuredBuffer <uint> g_clipped_flag;

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 2)]] RWStructuredBuffer <NodeContext> g_active_leafs;
[[vk::binding (1, 2)]] RWStructuredBuffer <VkDispatchIndirectCommand> g_active_leaf_counter;
[[vk::binding (2, 2)]] RWStructuredBuffer <uint> g_active_leaf_vertices_count;
[[vk::binding (3, 2)]] RWStructuredBuffer <uint> g_active_leaf_indices_count;

[[vk::binding (0, 3)]] ConstantBuffer <FrustumGeometry> frustum_geometry;

[[vk::binding (0, 4)]] Sampler2D <float> depth_sampler;

void process_voxel (uint3 group_id, NodeContext context, SdfOctreeNode leaf_node) {
    context.cube_index = 0;

    for (int i = 0; i < 8; ++i) {
        context.cube_index |= (leaf_node.values[i] < 0.0f ? (1 << i) : 0);
    }

    if (context.cube_index == 0 || context.cube_index == 255) {
        return;
    }

    uint active_leaf_offset = 0;
    InterlockedAdd (g_active_leaf_counter [0].x, 1, active_leaf_offset);

    if (active_leaf_offset >= pc.active_leafs_max_count) {
        return;
    }

    g_active_leafs [active_leaf_offset] = context;

    const uint2 mesh_counts = cube_index_2_mesh_output_counts [context.cube_index];
    g_active_leaf_vertices_count [active_leaf_offset] = mesh_counts.x;
    g_active_leaf_indices_count [active_leaf_offset] = mesh_counts.y * 3;
}

void dfs_octree (in uint3 group_id, NodeContext root) {
    if (g_octree_data [root.node_index].offset == 0) {
        process_voxel (group_id, root, g_octree_data [root.node_index]);
        return;
    }

    int node_index_stack [16];
    float3 coord_stack [16];
    int path_stack [16];

    node_index_stack [0] = root.node_index;
    coord_stack [0] = float3 (root.min_corner_x, root.min_corner_y, root.min_corner_z);
    path_stack [1] = 0;

    int level = 1; // starting with level 1
    while (level > 0) {
        int child_index = path_stack [level];
        if (child_index == 8 || level >= pc.max_octree_depth) { // no more children
            level -= 1;
            continue;
        }
        path_stack [level] += 1;

        SdfOctreeNode parent_node = g_octree_data [node_index_stack [level - 1]];
        int child_node_index = parent_node.offset + child_index;
        SdfOctreeNode child_node = g_octree_data [child_node_index];

        float child_voxel_size = root.voxel_size / (float)(1u << level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        child_corner_offset.x = (float)((child_index >> 0) & 1) * child_voxel_size;
        child_corner_offset.y = (float)((child_index >> 1) & 1) * child_voxel_size;
        child_corner_offset.z = (float)((child_index >> 2) & 1) * child_voxel_size;

        float3 child_coord = coord_stack [level - 1] + child_corner_offset;

        if (pc.frustum_culling == 1 || pc.occlusion_culling == 1) {
            float3 child_corners [8];
            for (int i = 0; i < 8; ++i) {
                child_corners [i].x = child_coord.x + (((i >> 0) & 1) * child_voxel_size);
                child_corners [i].y = child_coord.y + (((i >> 1) & 1) * child_voxel_size);
                child_corners [i].z = child_coord.z + (((i >> 2) & 1) * child_voxel_size);
            }

            if ((pc.frustum_culling == 1) && !voxel_frustum_intersects (frustum_geometry, child_corners)) {
                continue;
            }

            if ((pc.occlusion_culling == 1) && voxel_occluded (depth_sampler, pc.prev_view_proj, child_corners)) {
                continue;
            }
        }

        if (child_node.offset == 0) { // leaf
            NodeContext context;
            context.voxel_size = child_voxel_size;
            context.min_corner_x = child_coord.x;
            context.min_corner_y = child_coord.y;
            context.min_corner_z = child_coord.z;
            context.node_index = child_node_index;
            process_voxel (group_id, context, child_node);
        } else { // internal node
            node_index_stack [level] = child_node_index;
            coord_stack [level] = child_coord;

            level += 1;
            path_stack [level] = 0;
        }
    }
}

[shader ("compute")]
[numthreads (1, 1, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
    if (thread_id.x == 0) {
        g_active_leaf_counter [0].y = 1;
        g_active_leaf_counter [0].z = 1;
    }
    dfs_octree (thread_id, g_roots [thread_id.x]);
    return;
}

