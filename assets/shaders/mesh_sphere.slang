// mesh_sphere.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
[[vk::binding (2, 0)]] RWStructuredBuffer <LeafContext> g_active_leafs;
[[vk::binding (3, 0)]] RWStructuredBuffer <uint> g_insufficent_mem_flag;

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

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

static const float4 colors [MESH_WORKGROUP_SIZE] = {
    float4(1.0, 0.0, 0.0, 1.0),   // Красный
    float4(0.0, 1.0, 0.0, 1.0),   // Зеленый  
    float4(0.0, 0.0, 1.0, 1.0),   // Синий
    float4(1.0, 1.0, 0.0, 1.0),   // Желтый
    float4(1.0, 0.0, 1.0, 1.0),   // Магента
    float4(0.0, 1.0, 1.0, 1.0),   // Циан
    float4(1.0, 1.0, 1.0, 1.0),   // Белый
    float4(0.5, 0.5, 0.5, 1.0),   // Серый
    float4(1.0, 0.5, 0.0, 1.0),   // Оранжевый
    float4(1.0, 0.0, 0.5, 1.0),   // Розовый
    float4(0.5, 1.0, 0.0, 1.0),   // Лайм
    float4(0.0, 0.5, 1.0, 1.0),   // Голубой
    float4(1.0, 0.5, 0.5, 1.0),   // Светло-розовый
    float4(0.5, 1.0, 0.5, 1.0),   // Светло-зеленый
    float4(0.5, 0.5, 1.0, 1.0),   // Светло-голубой
    float4(0.0, 0.0, 0.0, 1.0)    // Черный
};

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (MESH_WORKGROUP_SIZE, 1, 1)]
void main(
    in payload TaskPayload payload,
    in uint3 group_id : SV_GroupID,
    in uint3 thread_id : SV_GroupThreadID,
    out vertices Vertex vertices [MAX_VERTS_PER_MESHLET],
    out indices uint3 triangles [MAX_PRIMS_PER_MESHLET])
{
    SetMeshOutputCounts (payload.vertices_count, payload.triangles_count);

    const uint local_leaf_index = group_id.x * MESH_WORKGROUP_SIZE + thread_id.x;
    if (local_leaf_index >= payload.leaf_count) {
        return;
    }
    const LeafContext leaf_context = g_active_leafs [payload.leaf_buffer_offset + local_leaf_index];
    const NodeContext leaf = leaf_context.node_context;

    const SdfOctreeNode node = g_octree_data [leaf.node_index];
    const float3 voxel_size_modifier = {leaf.voxel_size};
    float3 corners [8];

    for (int i = 0; i < 8; ++i) {
        float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) == 1) corner_offset.x = leaf.voxel_size;
        if (((i >> 1) & 1) == 1) corner_offset.y = leaf.voxel_size;
        if (((i >> 2) & 1) == 1) corner_offset.z = leaf.voxel_size;
        corners [i] = leaf.min_corner + corner_offset;
    }

    uint triangles_count = 0;
    for (; triangles_count < MAX_LEAF_PRIMS; ++triangles_count) {
        if (cube_index_2_triangle_indices [leaf.cube_index * 16 + triangles_count * 3] == -1) break;
    }

    for (int i = 0; i < triangles_count; ++i) {
        const int base = leaf.cube_index * 16 + i * 3;
        const uint3 tri = uint3 (leaf_context.vertices_local_offset)
            + uint3 (cube_index_2_triangle_indices [base + 0], cube_index_2_triangle_indices [base + 1], cube_index_2_triangle_indices [base + 2]);
        triangles [leaf_context.triangles_local_offset + i] = tri;
    }

    const int edge_mask = cube_index_2_edge_mask [leaf.cube_index]; // non-zero because of check in task shader
    int edge_bit = 1;

    for (int i = 0; i < MAX_LEAF_VERTS; ++i) {
        if ((edge_mask & edge_bit) == 0) {
            edge_bit <<= 1;
            continue;
        }

        const uint2 corner_indices = edge_corners [i];
        Vertex vertex;
        vertex.position = mul (pc.view_proj, float4 (interpolate_vertex (corners [corner_indices.x]
                                                , corners [corner_indices.y]
                                                , node.values [corner_indices.x]
                                                , node.values [corner_indices.y]
                                                ), 1.0f));
        vertex.color = colors [(payload.leaf_buffer_offset / pc.max_leaf_count_per_task) % 16];
        // vertex.color = colors [group_id.x];
        vertices [leaf_context.vertices_local_offset + i] = vertex;

        edge_bit <<= 1;
    }
}

