#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> nodes;
[[vk::binding (1, 0)]] StructuredBuffer <Payload> subtrees;
[[vk::binding (2, 0)]] RWStructuredBuffer <int> node_index_stack;
[[vk::binding (3, 0)]] RWStructuredBuffer <float3> coord_stack;
[[vk::binding (4, 0)]] RWStructuredBuffer <int> path_stack;

void dfs_octree (Payload root, int stack_offset) {
    payload Payload voxel;

    if (nodes [root.node_index].offset == 0) {
        voxel = root;
        DispatchMesh (1, 1, 1, voxel); // root is leaf
    }

    node_index_stack [stack_offset + 0] = root.node_index;
    coord_stack [stack_offset + 0] = root.min_corner;
    path_stack [stack_offset + 1] = 0;

    int level = 1; // starting with level 1
    while (level > 0) {
        int child_index = path_stack [stack_offset + level];
        if (child_index == 8 || level >= MAX_OCTREE_DEPTH) { // no more children
            level -= 1;
            continue;
        }
        path_stack [stack_offset + level] += 1;

        SdfOctreeNode parent_node = nodes [node_index_stack [stack_offset + level - 1]];
        int child_node_index = parent_node.offset + child_index;
        SdfOctreeNode child_node = nodes [child_node_index];

        float child_voxel_size = root.voxel_size / (1 << level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        if (((child_index >> 0) & 1) > 0) child_corner_offset.x = child_voxel_size;
        if (((child_index >> 1) & 1) > 0) child_corner_offset.y = child_voxel_size;
        if (((child_index >> 2) & 1) > 0) child_corner_offset.z = child_voxel_size;

        float3 child_coord = coord_stack [stack_offset + level - 1] + child_corner_offset;

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

            voxel.voxel_size = child_voxel_size;
            voxel.min_corner = child_coord;
            voxel.node_index = child_node_index;
            voxel.cube_index = cube_index;
            DispatchMesh (1, 1, 1, voxel);
        } else { // internal node
            node_index_stack [stack_offset + level] = child_node_index;
            coord_stack [stack_offset + level] = child_coord;

            level += 1;
            path_stack [stack_offset + level] = 0;
        }
    }
}

[shader ("amplification")]
[numthreads (1,1,1)]
void main (in uint id : SV_GroupIndex) {
    if (pc.frame_stack_offset != 0) return;
    // dfs_octree (subtrees [id.x], pc.frame_stack_offset + id.x * MAX_OCTREE_DEPTH);
    // dfs_octree (subtrees [0], pc.frame_stack_offset + 0 * MAX_OCTREE_DEPTH);
    dfs_octree (subtrees [0], 0);
}

