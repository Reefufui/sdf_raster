// task_generator.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;

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
[[vk::binding (2, 3)]] RWStructuredBuffer <uint> g_active_leaf_vertices_offsets;
[[vk::binding (3, 3)]] RWStructuredBuffer <uint> g_active_leaf_indices_offsets;
[[vk::binding (4, 3)]] RWStructuredBuffer <uint> g_active_leaf_overflow_flag;

[[vk::binding (0, 4)]] RWStructuredBuffer <VkDrawIndexedIndirectCommand> g_draw_indexed_indirect_command;

#define WORKGROUP_SIZE 256

static const float4 colors [16] = {
    float4 (1.0, 0.0, 0.0, 1.0),   // Красный
    float4 (0.0, 1.0, 0.0, 1.0),   // Зеленый
    float4 (0.0, 0.0, 1.0, 1.0),   // Синий
    float4 (1.0, 1.0, 0.0, 1.0),   // Желтый
    float4 (1.0, 0.0, 1.0, 1.0),   // Магента
    float4 (0.0, 1.0, 1.0, 1.0),   // Циан
    float4 (1.0, 1.0, 1.0, 1.0),   // Белый
    float4 (0.5, 0.5, 0.5, 1.0),   // Серый
    float4 (1.0, 0.5, 0.0, 1.0),   // Оранжевый
    float4 (1.0, 0.0, 0.5, 1.0),   // Розовый
    float4 (0.5, 1.0, 0.0, 1.0),   // Лайм
    float4 (0.0, 0.5, 1.0, 1.0),   // Голубой
    float4 (1.0, 0.5, 0.5, 1.0),   // Светло-розовый
    float4 (0.5, 1.0, 0.5, 1.0),   // Светло-зеленый
    float4 (0.5, 0.5, 1.0, 1.0),   // Светло-голубой
    float4 (0.0, 0.0, 0.0, 1.0)    // Черный
};

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

void generate_geometry (uint active_leaf_index) {
	NodeContext context = g_active_leafs [active_leaf_index];
	// uint vertices_offset = g_active_leaf_vertices_offsets [active_leaf_index];
	// uint indices_offset = g_active_leaf_indices_offsets [active_leaf_index];
	uint vertices_offset = active_leaf_index * MAX_LEAF_VERTS;
	uint indices_offset = active_leaf_index * MAX_LEAF_PRIMS * 3;

    const float3 min_corner = {context.min_corner_x, context.min_corner_y, context.min_corner_z};

    float3 corners [8];
    for (int i = 0; i < 8; ++i) {
        float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) == 1) corner_offset.x = context.voxel_size;
        if (((i >> 1) & 1) == 1) corner_offset.y = context.voxel_size;
        if (((i >> 2) & 1) == 1) corner_offset.z = context.voxel_size;
        corners [i] = min_corner + corner_offset;
    }

    uint triangles_count = 0;
    for (; triangles_count < MAX_LEAF_PRIMS; ++triangles_count) {
        if (cube_index_2_triangle_indices [context.cube_index * 16 + triangles_count * 3] == -1) break;
    }

    for (int i = 0; i < triangles_count; ++i) {
        const int base = context.cube_index * 16 + i * 3;
        g_indices [indices_offset + i * 3 + 0] = vertices_offset + cube_index_2_triangle_indices [base + 0];
        g_indices [indices_offset + i * 3 + 1] = vertices_offset + cube_index_2_triangle_indices [base + 1];
        g_indices [indices_offset + i * 3 + 2] = vertices_offset + cube_index_2_triangle_indices [base + 2];
    }

    const SdfOctreeNode leaf_node = g_octree_data [context.node_index];
    const int edge_mask = cube_index_2_edge_mask [context.cube_index];
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
                                                , leaf_node.values [corner_indices.x]
                                                , leaf_node.values [corner_indices.y]
                                                ), 1.0f));
        vertex.normal = float4 (1.f, 1.f, 1.f, 1.f); // TODO
        vertex.color = colors [context.node_index % 16];
        g_vertices [vertices_offset + i] = vertex;

        edge_bit <<= 1;
    }

	if (active_leaf_index == g_active_leaf_counter [0].x - 1) {
		g_draw_indexed_indirect_command [0] = {indices_offset + triangles_count * 3, 1, 0, 0, 0};
	}
}

[shader ("compute")]
[numthreads (WORKGROUP_SIZE, 1, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
	if (thread_id.x >= g_active_leaf_counter [0].x) return;
	generate_geometry (thread_id.x);
}

