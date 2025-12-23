#include <iostream>
#include <fstream>

#include "cpu_sandbox.h"
#include "marching_cubes_lookup_table.hpp"

namespace cpu_sandbox {

float3 interpolate_vertex (float3 p1, float3 p2, float valp1, float valp2) {
    if (std::abs (0.f - valp1) < 0.00001) {
        return (p1);
    }
    if (std::abs (0.f - valp2) < 0.00001) {
        return (p2);
    }
    if (std::abs (valp1 - valp2) < 0.00001) {
        return (p1);
    }

    float mu = (0.f - valp1) / (valp2 - valp1);

    float3 p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    p.z = p1.z + mu * (p2.z - p1.z);
    return (p);
}

void dispatch_mesh (NodeContext payload, std::vector <SdfOctreeNode>& nodes) {
    std::vector <Vertex> verts (MAX_LEAF_VERTS);
    std::vector <uint3> triangles (MAX_LEAF_PRIMS);

    SdfOctreeNode node = nodes [payload.node_index];
    const float3 voxel_size_modifier {payload.voxel_size};
    float3 corners [8];

    for (int i = 0; i < 8; ++i) {
        float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) == 1) corner_offset.x = payload.voxel_size;
        if (((i >> 1) & 1) == 1) corner_offset.y = payload.voxel_size;
        if (((i >> 2) & 1) == 1) corner_offset.z = payload.voxel_size;
        corners [i] = payload.min_corner + corner_offset;
    }

    uint triangles_count = 0;

    for (; triangles_count < 4; ++triangles_count) {
        if (cube_index_2_triangle_indices [payload.cube_index][triangles_count * 3] == -1) break;
        triangles [triangles_count] = uint3 (cube_index_2_triangle_indices [payload.cube_index][triangles_count * 3 + 0]
                , cube_index_2_triangle_indices [payload.cube_index][triangles_count * 3 + 1]
                , cube_index_2_triangle_indices [payload.cube_index][triangles_count * 3 + 2]);
    }

    int edge_mask = cube_index_2_edge_mask [payload.cube_index];
    if (edge_mask == 0) {
        return;
    }

    int edge_bit = 1;
    for (int i = 0; i < 12; ++i) {
        if ((edge_mask & edge_bit) == 0) {
            edge_bit <<= 1;
            continue;
        }

        const uint2 corner_indices = edge_corners [i];
        verts [i].position = LiteMath::to_float4 (interpolate_vertex (corners [corner_indices.x]
                                                , corners [corner_indices.y]
                                                , node.values [corner_indices.x]
                                                , node.values [corner_indices.y]
                                                ), 1.0f);
        verts [i].color = float4 (1.0f, 1.0f, 0.0f, 1.0f);
        edge_bit <<= 1;
    }

    size_t offset = get_vertex_count ();

    for (size_t i = 0; i < MAX_LEAF_VERTS; ++i) {
        add_vertex (verts [i].position);
    }

    for (size_t i = 0; i < triangles_count; ++i) {
        uint3 offseted_triangle_indices (
            triangles [i].x + offset,
            triangles [i].y + offset,
            triangles [i].z + offset
        );
        add_triangle (offseted_triangle_indices);
    }
}

}

