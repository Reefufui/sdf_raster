#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> nodes;

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256] DEPRECATED!!!

const static uint MAX_VERTS = 12;
const static uint MAX_PRIMS = 4;

float3 interpolate_vertex (float3 p1, float3 p2, float valp1, float valp2) {
    if (abs (0.f - valp1) < 0.00001)
        return (p1);
    if (abs (0.f - valp2) < 0.00001)
        return (p2);
    if (abs (valp1 - valp2) < 0.00001)
        return (p1);

    float mu = (0.f - valp1) / (valp2 - valp1);

    float3 p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    p.z = p1.z + mu * (p2.z - p1.z);
    return (p);
}

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (1,1,1)]
void main(
    in uint tig : SV_GroupIndex,
    in payload Payload payload,
    OutputVertices <Vertex, MAX_VERTS> verts,
    OutputIndices <uint3, MAX_PRIMS> triangles)
{
    SdfOctreeNode node = nodes [payload.node_index];
    const float3 voxel_size_modifier = {payload.voxel_size};
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
        int base = payload.cube_index * 16 + triangles_count * 3;
        if (cube_index_2_triangle_indices [base] == -1) break;
        triangles [triangles_count] = uint3 (cube_index_2_triangle_indices [base + 0]
                , cube_index_2_triangle_indices [base + 1]
                , cube_index_2_triangle_indices [base + 2]);
    }

    SetMeshOutputCounts (12, triangles_count);

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
        verts [i].position = mul (pc.view_proj, float4 (interpolate_vertex (corners [corner_indices.x]
                                                , corners [corner_indices.y]
                                                , node.values [corner_indices.x]
                                                , node.values [corner_indices.y]
                                                ), 1.0f));
        verts [i].color = float4 (1.0f, 1.0f, 0.0f, 1.0f);
        edge_bit <<= 1;
    }

}

