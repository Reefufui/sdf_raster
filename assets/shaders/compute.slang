// compute.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
[[vk::binding (2, 0)]] StructuredBuffer <uint> g_clipped_flag;

[[vk::binding (0, 1)]] RWStructuredBuffer <Vertex> g_vertices;
[[vk::binding (1, 1)]] RWStructuredBuffer <uint> g_indices;
[[vk::binding (2, 1)]] RWStructuredBuffer <uint> g_indices_count;
[[vk::binding (3, 1)]] RWStructuredBuffer <uint> g_insufficent_mesh_mem_flag;

[[vk::binding (0, 2)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 2)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 2)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 2)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 3)]] RWStructuredBuffer <NodeContext> g_active_leafs;
[[vk::binding (1, 3)]] RWStructuredBuffer <VkDispatchIndirectCommand> g_active_leaf_counter;
[[vk::binding (2, 3)]] RWStructuredBuffer <uint> g_active_leaf_vertices_count;
[[vk::binding (3, 3)]] RWStructuredBuffer <uint> g_active_leaf_indices_count;
[[vk::binding (4, 3)]] RWStructuredBuffer <uint> g_active_leaf_overflow_flag;

[[vk::binding (0, 4)]] ConstantBuffer <FrustumGeometry> frustum_geometry;

[[vk::binding (0, 5)]] Sampler2D <float> depth_sampler;

#include "frustum_culling.h"
#include "occlusion_culling.h"
#include "flatshadable_voxel.h"

void process_voxel (uint3 group_id, NodeContext context, SdfOctreeNode leaf_node) {
    context.cube_index = 0;

    for (int i = 0; i < 8; ++i) {
        if (leaf_node.values [i] < 0.0f) {
            context.cube_index |= (1 << i);
        }
    }

    if (context.cube_index == 0 || context.cube_index == 255) {
        return;
    }

    if (g_active_leaf_overflow_flag [0] > 0) {
        return;
    }

    uint active_leaf_offset = 0;
    InterlockedAdd (g_active_leaf_counter [0].x, 1, active_leaf_offset);

    if (active_leaf_offset >= pc.active_leafs_max_count) {
        g_active_leaf_overflow_flag [0] = 1;
        return;
    }

    g_active_leafs [active_leaf_offset] = context;
    g_active_leaf_vertices_count [active_leaf_offset] = cube_index_2_mesh_output_counts [context.cube_index].x;
    g_active_leaf_indices_count [active_leaf_offset] = cube_index_2_mesh_output_counts [context.cube_index].y * 3;
}

void dfs_octree (in uint3 group_id, NodeContext root) {
    if (g_octree_data [root.node_index].offset == 0) {
        process_voxel (group_id, root, g_octree_data [root.node_index]);
        return;
    }

    int node_index_stack [MAX_OCTREE_DEPTH];
    float3 coord_stack [MAX_OCTREE_DEPTH];
    int path_stack [MAX_OCTREE_DEPTH];

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

        float child_voxel_size = root.voxel_size / (1 << level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        if (((child_index >> 0) & 1) > 0) child_corner_offset.x = child_voxel_size;
        if (((child_index >> 1) & 1) > 0) child_corner_offset.y = child_voxel_size;
        if (((child_index >> 2) & 1) > 0) child_corner_offset.z = child_voxel_size;

        float3 child_coord = coord_stack [level - 1] + child_corner_offset;

        if (!voxel_frustum_intersects (child_coord, child_voxel_size)) {
            continue;
        }

        if (voxel_occluded (child_coord, child_voxel_size)) {
            continue;
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
    // flatshadable_voxel (g_roots [thread_id.x], thread_id.x, colors [thread_id.x % 16]);
    return;
}

