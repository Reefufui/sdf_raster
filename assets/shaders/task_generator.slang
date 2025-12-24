// task_generator.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
[[vk::binding (2, 0)]] RWStructuredBuffer <NodeContext> g_active_leafs;
[[vk::binding (3, 0)]] RWStructuredBuffer <uint> g_insufficent_mem_flag;

groupshared TaskPayload s_payload;

uint dfs_octree (NodeContext root, uint task_offset) {
    if (g_octree_data [root.node_index].offset == 0) {
        g_active_leafs [task_offset] = root;
        return 1;
    }

    uint leaf_count = 0;

    int node_index_stack [MAX_OCTREE_DEPTH];
    float3 coord_stack [MAX_OCTREE_DEPTH];
    int path_stack [MAX_OCTREE_DEPTH];

    node_index_stack [0] = root.node_index;
    coord_stack [0] = root.min_corner;
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

        if (child_node.offset == 0) { // leaf
            int cube_index = 0;

            for (int i = 0; i < 8; ++i) {
                if (child_node.values [i] < 0.0f) {
                    cube_index |= (1 << i);
                }
            }

            if (cube_index == 0 || cube_index == 255) {
                continue;
            }

            NodeContext active_leaf;
            active_leaf.voxel_size = child_voxel_size;
            active_leaf.min_corner = child_coord;
            active_leaf.node_index = child_node_index;
            active_leaf.cube_index = cube_index;
            if (leaf_count < pc.max_leaf_count_per_task) {
                g_active_leafs [task_offset + leaf_count] = active_leaf;
            } else {
                g_insufficent_mem_flag [0] = 1;
                return leaf_count;
            }

            leaf_count += 1;
        } else { // internal node
            node_index_stack [level] = child_node_index;
            coord_stack [level] = child_coord;

            level += 1;
            path_stack [level] = 0;
        }
    }

    return leaf_count;
}

[shader ("amplification")]
[numthreads (1, 1, 1)]
void main (in uint3 group_id : SV_GroupID) {
    uint task_offset = pc.max_leaf_count_per_task * group_id.x;
    uint leaf_count = dfs_octree (g_roots [group_id.x], task_offset);

    if (leaf_count > 0) {
        s_payload.leaf_buffer_offset = task_offset;
        s_payload.leaf_count = leaf_count;

        uint num_mesh_groups = (leaf_count + MESH_WORKGROUP_SIZE - 1) / MESH_WORKGROUP_SIZE;
        DispatchMesh (num_mesh_groups, 1, 1, s_payload);
    }
}

