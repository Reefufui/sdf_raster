// marching_cubes_octree.comp.slang

#include "shader_common.hpp"

#include "colors.slang"

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

[[vk::binding (0, 3)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 3)]] RWStructuredBuffer <NodeContext> g_active_leafs;

[[vk::binding (0, 4)]] RWStructuredBuffer <VkDrawIndexedIndirectCommand> g_draw_indexed_indirect_command;

[[vk::binding (0, 5)]] RWStructuredBuffer <LevelOfDetail> g_lod;

static const float eps = 1e-6f;

float3 interpolate (float3 p1, float3 p2, float valp1, float valp2) {
    if (abs (valp1 - valp2) < eps) {
        return p1;
    }
    float mu = -valp1 / (valp2 - valp1);
    mu = clamp (mu, 0.0f, 1.0f);
    return lerp (p1, p2, mu);
}

float sample_sdf (const float3 p) {
    SdfOctreeNode node = g_octree_data [0];
    float3 min_corner = {-1.0f, -1.0f, -1.0f};
    float voxel_size = 2.0f;
    uint level = 0;

    while (node.offset != 0 && level < g_lod [0].lod) {
        level += 1;
        float half = voxel_size * 0.5f;
        uint child_index = 0;
        if (p.x >= min_corner.x + half) {
            child_index |= 1;
            min_corner.x += half;
        }
        if (p.y >= min_corner.y + half) {
            child_index |= 2;
            min_corner.y += half;
        }
        if (p.z >= min_corner.z + half) {
            child_index |= 4;
            min_corner.z += half;
        }
        voxel_size = half;
        node = g_octree_data [node.offset + child_index];
    }

    float3 local = (p - min_corner) / voxel_size;

    float c00 = lerp (node.values [0], node.values [1], local.x);
    float c01 = lerp (node.values [4], node.values [5], local.x);
    float c10 = lerp (node.values [2], node.values [3], local.x);
    float c11 = lerp (node.values [6], node.values [7], local.x);

    float c0 = lerp (c00, c10, local.y);
    float c1 = lerp (c01, c11, local.y);

    return lerp (c0, c1, local.z);
}

float3 sample_normal (float3 p) {
    float dx = sample_sdf (p + float3 (eps, 0, 0)) - sample_sdf (p - float3 (eps, 0, 0));
    float dy = sample_sdf (p + float3 (0, eps, 0)) - sample_sdf (p - float3 (0, eps, 0));
    float dz = sample_sdf (p + float3 (0, 0, eps)) - sample_sdf (p - float3 (0, 0, eps));
    return normalize (float3 (dx, dy, dz) / (2 * eps));
}

void generate_geometry (uint active_leaf_index) {
	NodeContext context = g_active_leafs [active_leaf_index];
	uint vertices_offset = active_leaf_index * MAX_LEAF_VERTS;
	uint indices_offset = active_leaf_index * MAX_LEAF_PRIMS * 3;

    const float3 min_corner = {context.min_corner_x, context.min_corner_y, context.min_corner_z};

    float3 corners [8];
    for (int i = 0; i < 8; ++i) {
        corners [i] = min_corner + float3 (
            (float)((i >> 0) & 1)
            , (float)((i >> 1) & 1)
            , (float)((i >> 2) & 1)
        ) * context.voxel_size;
    }

    uint triangles_count = cube_index_2_mesh_output_counts [context.cube_index].y;

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
        // float3 position = (corners [corner_indices.x] + corners [corner_indices.y]) / 2.f;
        float3 position = interpolate (corners [corner_indices.x]
                     , corners [corner_indices.y]
                     , leaf_node.values [corner_indices.x]
                     , leaf_node.values [corner_indices.y]);

        Vertex vertex;
        vertex.position = float4 (position, 1.0f);
        vertex.normal = float4 (sample_normal (position), 1.0f);
        if (pc.color_leafs == 1u) {
            vertex.color = colors [context.node_index % COLORS_COUNT];
        } else if (pc.color_leafs == 2u) {
            vertex.color = voxel_size_color (context.voxel_size, pc.min_voxel_size, pc.max_voxel_size);
        } else {
            vertex.color = float4 (1.f);
        }
        g_vertices [vertices_offset + i] = vertex;

        edge_bit <<= 1;
    }

	if (active_leaf_index == g_active_leaf_counter [0] - 1) {
		g_draw_indexed_indirect_command [0] = {indices_offset + triangles_count * 3, 1, 0, 0, 0};
	}
}

[shader ("compute")]
[numthreads (VOXELS_PER_COMPUTE_WORKGROUP, 1, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
	if (thread_id.x >= g_active_leaf_counter [0]) return;
	generate_geometry (thread_id.x);
}

