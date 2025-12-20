#include <iostream>

#include "cpu_sandbox.h"

namespace cpu_sandbox {

void dfs_octree (Payload root, std::vector <SdfOctreeNode>& nodes) {
    if (nodes [root.node_index].offset == 0) {
        Payload voxel = root;
        dispatch_mesh (voxel, nodes); // root is leaf
    }

    int node_index_stack [MAX_OCTREE_DEPTH];
    float3 coord_stack [MAX_OCTREE_DEPTH];
    int path_stack [MAX_OCTREE_DEPTH]; // stack of values 0..7 - child num on current level

    node_index_stack [0] = root.node_index;
    coord_stack [0] = root.min_corner;
    path_stack [1] = 0;

    int level = 1; // starting with level 1
    while (level > 0) {
        int child_index = path_stack [level];
        if (child_index == 8 || level >= MAX_OCTREE_DEPTH) { // no more children
            level -= 1;
            continue;
        }
        path_stack [level] += 1;

        SdfOctreeNode parent_node = nodes [node_index_stack [level - 1]];
        int child_node_index = parent_node.offset + child_index;
        SdfOctreeNode child_node = nodes [child_node_index];

        float child_voxel_size = root.voxel_size / (1 << level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        if (((child_index >> 0) & 1) == 1) child_corner_offset.x = child_voxel_size;
        if (((child_index >> 1) & 1) == 1) child_corner_offset.y = child_voxel_size;
        if (((child_index >> 2) & 1) == 1) child_corner_offset.z = child_voxel_size;

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

            Payload voxel;
            voxel.voxel_size = child_voxel_size;
            voxel.min_corner = child_coord;
            voxel.node_index = child_node_index;
            voxel.cube_index = cube_index;
            dispatch_mesh (voxel, nodes);
        } else { // internal node
            node_index_stack [level] = child_node_index;
            coord_stack [level] = child_coord;

            level += 1;
            path_stack [level] = 0;
        }
    }
}

void task_generator (Payload subtree, std::vector <SdfOctreeNode>& nodes) {
    dfs_octree (subtree, nodes);
}

}

