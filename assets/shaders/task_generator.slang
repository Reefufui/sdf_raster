#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> nodes;

void dfs_octree (Payload root) {
    Payload stack [20];
    int ptr = 0;
    stack [ptr++] = root;

    while (ptr > 0) {
        Payload current_voxel = stack [--ptr];
        SdfOctreeNode current_node = nodes [current_voxel.node_index];

        if (current_node.offset == 0) { // leaf
            DispatchMesh (1, 1, 1, current_voxel);
            continue;
        }

        float child_size = current_voxel.voxel_size / 2.f;

        for (int i = 7; i >= 0; --i) {
            Payload child;

            float3 corner_offset = {0.0f, 0.0f, 0.0f};
            if ((i >> 0) & 1 == 1) corner_offset.x = child_size;
            if ((i >> 1) & 1 == 1) corner_offset.y = child_size;
            if ((i >> 2) & 1 == 1) corner_offset.z = child_size;

            child.min_corner = current_voxel.min_corner + corner_offset;
            child.node_index = current_node.offset + i;
            child.voxel_size = child_size;
            stack [ptr++] = child;
        }
    }
}

[shader ("amplification")]
[numthreads (1,1,1)]
void main () {
    Payload root;
    root.min_corner = float3 (-1.f, -1.f, -1.f);
    root.voxel_size = 2.f;
    root.node_index = 0;
    // dfs_octree (root);
        DispatchMesh (1, 1, 1, root);
}

