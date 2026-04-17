// marching_cubes.slang

#include "shader_common.hpp"

#include "scomtree_utils.slang"
#include "colors.slang"

#define SCOM2_EPS 1e-6f

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] ConstantBuffer <SComTreeHeader> g_scomtree_header;
[[vk::binding (1, 0)]] StructuredBuffer <uint> g_scomtree_nodes;
[[vk::binding (2, 0)]] StructuredBuffer <uint> g_scomtree_bricks;
[[vk::binding (3, 0)]] StructuredBuffer <int4> g_rotation_modifiers; // NOTE: 3 * 48
[[vk::binding (4, 0)]] StructuredBuffer <uint> g_rotation_add; // NOTE: 2304
[[vk::binding (5, 0)]] StructuredBuffer <SComTreeStackElement> g_roots;

[[vk::binding (0, 1)]] RWStructuredBuffer <Vertex> g_vertices;
[[vk::binding (1, 1)]] RWStructuredBuffer <uint> g_indices;
[[vk::binding (2, 1)]] RWStructuredBuffer <uint> g_indices_count;
[[vk::binding (3, 1)]] RWStructuredBuffer <uint> g_insufficent_mesh_mem_flag;

[[vk::binding (0, 2)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 2)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 2)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 2)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 3)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 3)]] RWStructuredBuffer <SComTreeBrickPayload> g_active_leafs;

[[vk::binding (0, 4)]] RWStructuredBuffer <VkDrawIndexedIndirectCommand> g_draw_indexed_indirect_command;

[[vk::binding (0, 5)]] RWStructuredBuffer <LevelOfDetail> g_lod;

static const float eps = 1e-6f;

float sample_sdf (const float3 p) {
    return 0.f; // TODO:
}

float3 sample_normal (float3 p) {
    return float3 (0.f); // TODO:
}

float3 interpolate_vertex (float isolevel, float3 p1, float3 p2, float valp1, float valp2) {
    if (abs (isolevel - valp1) < 0.00001)
        return (p1);
    if (abs (isolevel - valp2) < 0.00001)
        return (p2);
    if (abs (valp1 - valp2) < 0.00001)
        return (p1);

    float mu = (isolevel - valp1) / (valp2 - valp1);

    float3 p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    p.z = p1.z + mu * (p2.z - p1.z);
    return (p);
}

/*
float eval_sdf_local (VoxelContext ctx, float3 p) {
    float3 local_p = (p - ctx.min_pos) / ctx.size;
    float tx = local_p.x, ty = local_p.y, tz = local_p.z;

    float c00 = ctx.sdf [0] * (1 - tx) + ctx.sdf [4] * tx;
    float c01 = ctx.sdf [1] * (1 - tx) + ctx.sdf [5] * tx;
    float c10 = ctx.sdf [2] * (1 - tx) + ctx.sdf [6] * tx;
    float c11 = ctx.sdf [3] * (1 - tx) + ctx.sdf [7] * tx;

    float c0 = c00 * (1 - ty) + c10 * ty;
    float c1 = c01 * (1 - ty) + c11 * ty;

    return c0 * (1 - tz) + c1 * tz;
}
*/

void generate_geometry (uint active_leaf_index) {
	SComTreeBrickPayload payload = g_active_leafs [active_leaf_index];
	uint vertices_offset = active_leaf_index * MAX_BRICK_VERTS;
	uint indices_offset = active_leaf_index * MAX_BRICK_PRIMS * 3;

    const uint3 p = uint3 (payload.p_size.x >> 16, payload.p_size.x & 0xFFFF, payload.p_size.y >> 16);
    const uint32_t level_sz = payload.p_size.y & 0xFFFF;

    const float d = 2.0f / float (level_sz);
    const float3 p_f = float3 (p);
    const float max_val = get_max_sdf_val (float (level_sz));

    const uint link = payload.rot_link & 0x3FFFFFF;
    const uint rotation = payload.rot_link >> 26;

    uint32_t link_data = g_scomtree_nodes [link];
    SdfDAGDataEdge de = unpack_data_edge (g_scomtree_header, max_val, link_data, link_data);
    uint32_t offset = g_scomtree_header.bricks_step * de.data_offset;
    uint32_t rotation_index = g_rotation_add [rotation * 48 + de.rotation_id];
    float add = de.add;

    for (uint32_t vox_idx = 0; vox_idx < 8; ++vox_idx) {
        float vmin = float.minValue;
        float vmax = float.maxValue;
        float sdf_values [8] = {};

        int3 vox_p = int3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));

        for (int i = 0; i < 8; ++i) {
            int4 value_p = int4 (vox_p, 0) + int4 (((i & 4) >> 2), ((i & 2) >> 1), (i & 1), 1);
            uint32_t vId0 = uint32_t (dot (g_rotation_modifiers [rotation_index], value_p));
            const uint32_t p_val = g_scomtree_bricks [offset + vId0 / g_scomtree_header.values_per_uint];
            const uint32_t p_off = (vId0 % g_scomtree_header.values_per_uint) * g_scomtree_header.bits_per_value;
            float val = max_val * (2.0f * ((p_val >> p_off) & g_scomtree_header.value_mask) / float(g_scomtree_header.value_mask) - 1) + add;
            sdf_values [i] = val - 0.5f * SCOM2_EPS * d;
            vmin = fmin (vmin, val);
            vmax = fmax (vmax, val);
        }

        if (vmin <= 0.0f && vmax >= 0.0f) {
            const float3 voxel_min_pos = float3 (-1, -1, -1) + d * p_f + 0.5f * d * float3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));
            const float voxel_size = 0.5f * d;

            int cube_index = 0;
            float3 corners [8];

            for (int i = 0; i < 8; ++i) {
                float3 corner_offset = {0.0f, 0.0f, 0.0f};
                if (bool ((i >> 2) & 1)) corner_offset.x = voxel_size;
                if (bool ((i >> 1) & 1)) corner_offset.y = voxel_size;
                if (bool ((i >> 0) & 1)) corner_offset.z = voxel_size;
                corners [i] = voxel_min_pos + corner_offset;

                if (sdf_values [i] < 0.f) {
                    cube_index |= (1 << i);
                }
            }

            uint triangles_count = cube_index_2_mesh_output_counts [cube_index].y;

            for (int i = 0; i < triangles_count; ++i) {
                const int base = cube_index * 16 + i * 3;
                g_indices [indices_offset + i * 3 + 0] = vertices_offset + cube_index_2_triangle_indices [base + 0];
                g_indices [indices_offset + i * 3 + 1] = vertices_offset + cube_index_2_triangle_indices [base + 1];
                g_indices [indices_offset + i * 3 + 2] = vertices_offset + cube_index_2_triangle_indices [base + 2];
            }

            int edge_mask = cube_index_2_edge_mask [cube_index];
            if (edge_mask == 0) {
                return;
            }
            int edge_bit = 1;

            for (int i = 0; i < MAX_LEAF_VERTS; ++i) {
                if ((edge_mask & edge_bit) == 0) {
                    edge_bit <<= 1;
                    continue;
                }

                const uint2 corner_indices = edge_corners [i];
                float3 position = interpolate_vertex (0.f, corners [corner_indices.x]
                    , corners [corner_indices.y]
                    , sdf_values [corner_indices.x]
                    , sdf_values [corner_indices.y]);

                Vertex vertex;
                vertex.position = mul (pc.view_proj, float4 (position, 1.0f));
                // vertex.normal = float4 (sample_normal (position), 1.0f); // TODO:
                vertex.color = float4 (1.f);

                g_vertices [vertices_offset + MAX_LEAF_VERTS * vox_idx + i] = vertex;

                edge_bit <<= 1;
            }

            /*
            const int* triangle_indices = cube_index_2_triangle_indices [cube_index];
            for (int i = 0; triangle_indices [i] != -1; i += 3) {
                for (int j = 0; j < 3; ++j) {
                    auto pos = edge_vertices [triangle_indices [i + j]];
                    tri [j].position = { pos.x, pos.y, pos.z, 1.f };

                    const float h = ctx.size * 0.01f;

                    float dx = eval_sdf_local (ctx, pos + float3 {h, 0, 0})
                        - eval_sdf_local (ctx, pos - float3 {h, 0, 0});
                    float dy = eval_sdf_local (ctx, pos + float3 {0, h, 0})
                        - eval_sdf_local (ctx, pos - float3 {0, h, 0});
                    float dz = eval_sdf_local (ctx, pos + float3 {0, 0, h})
                        - eval_sdf_local (ctx, pos - float3 {0, 0, h});

                    float3 norm = normalize (float3 {dx, dy, dz});
                    tri [j].normal = { norm.x, norm.y, norm.z, 0.0f };

                    // tri [j].color = float4 (1.f, 1.f, 1.f, 1.f);
                    tri [j].color = to_float4 ((norm / 2.f) + 0.5f, 1.f);
                }
            }
            */
        }
    }

	if (active_leaf_index == g_active_leaf_counter [0] - 1) {
		g_draw_indexed_indirect_command [0] = {(indices_offset + MAX_BRICK_PRIMS) * 3, 1, 0, 0, 0};
	}
}

[shader ("compute")]
[numthreads (BRICKS_PER_COMPUTE_WORKGROUP, 1, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
	if (thread_id.x >= g_active_leaf_counter [0]) return;
	generate_geometry (thread_id.x);
}

