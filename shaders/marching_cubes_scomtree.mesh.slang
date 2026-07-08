// marching_cubes_scomtree.mesh.slang

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

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 2)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 2)]] RWStructuredBuffer <SComTreeBrickPayload> g_active_leafs;

struct MeshOutput {
    float4 clip_position  : SV_Position;
    float4 position       : TEXCOORD0;
    float4 normal         : TEXCOORD1;
    float4 color          : COLOR0;
};

#define MAX_VERTS_PER_VOXEL 12
#define MAX_PRIMS_PER_VOXEL 4
#define MAX_VERTS_PER_BRICK (8 * MAX_VERTS_PER_VOXEL)
#define MAX_PRIMS_PER_BRICK (8 * MAX_PRIMS_PER_VOXEL)

// least 16 bit: vertex count
// most 16 bit: primitive count
groupshared uint s_packed_offsets;

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

float eval_local_voxel_sdf (float sdf [8], float3 min_pos, float size, float3 p) {
    float3 local_p = (p - min_pos) / size;
    float tx = local_p.x, ty = local_p.y, tz = local_p.z;

    float c00 = sdf [0] * (1 - tx) + sdf [4] * tx;
    float c01 = sdf [1] * (1 - tx) + sdf [5] * tx;
    float c10 = sdf [2] * (1 - tx) + sdf [6] * tx;
    float c11 = sdf [3] * (1 - tx) + sdf [7] * tx;

    float c0 = c00 * (1 - ty) + c10 * ty;
    float c1 = c01 * (1 - ty) + c11 * ty;

    return c0 * (1 - tz) + c1 * tz;
}

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (8, 1, 1)]
void main (
    uint3 gi : SV_GroupID,
    uint3 gti : SV_GroupThreadID,
    uint3 dti : SV_DispatchThreadID,
    OutputVertices <MeshOutput, MAX_VERTS_PER_BRICK> out_vertices,
    OutputIndices <uint3, MAX_PRIMS_PER_BRICK> out_primitives) {
	if (gti.x == 0) {
	    s_packed_offsets = 0;
	}

	GroupMemoryBarrierWithGroupSync ();

	SComTreeBrickPayload brick = g_active_leafs [gi.x];
	const uint vox_idx = gti.x;

    const uint link = brick.rot_link & 0x3FFFFFF;
    const uint rotation = brick.rot_link >> 26;

    const uint32_t link_data = g_scomtree_nodes [link];
    const float max_val = get_max_sdf_val (float (brick.p_size.y & 0xFFFF));
    const SdfDAGDataEdge de = unpack_data_edge (g_scomtree_header, max_val, link_data, link_data);
    const uint32_t brick_offset = g_scomtree_header.bricks_step * de.data_offset;
    const uint32_t rot_idx = g_rotation_add [rotation * 48 + de.rotation_id];

    const uint3 p = uint3 (brick.p_size.x >> 16, brick.p_size.x & 0xFFFF, brick.p_size.y >> 16);
    const uint32_t level_sz = brick.p_size.y & 0xFFFF;

    const float d = 2.0f / float (level_sz);
    const float3 p_f = float3 (p);

    const int3 vox_p = int3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));

    float vmin = 1e30;
    float vmax = -1e30;
    float sdf_values [8] = {};

    for (int i = 0; i < 8; ++i) {
        int4 value_p = int4 (vox_p, 0) + int4 (((i & 4) >> 2), ((i & 2) >> 1), (i & 1), 1);
        uint32_t vId0 = uint32_t (dot (g_rotation_modifiers [rot_idx], value_p));
        const uint32_t p_val = g_scomtree_bricks [brick_offset + vId0 / g_scomtree_header.values_per_uint];
        const uint32_t p_off = (vId0 % g_scomtree_header.values_per_uint) * g_scomtree_header.bits_per_value;
        float val = max_val * (2.0f * ((p_val >> p_off) & g_scomtree_header.value_mask) / float(g_scomtree_header.value_mask) - 1) + de.add;
        sdf_values [i] = val - 0.5f * SCOM2_EPS * d;
        vmin = fmin (vmin, val);
        vmax = fmax (vmax, val);
    }

	bool has_geometry = (vmin <= 0.0f && vmax >= 0.0f);
	float3 voxel_min_pos;
	float voxel_size = 0;
    int cube_index = 0;
    float3 corners [8];
    uint vertices_count = 0;
    uint primitives_count = 0;
	uint vertex_offset = 0;
	uint primitive_offset = 0;

    if (has_geometry) {
        voxel_min_pos = float3 (-1, -1, -1) + d * p_f + 0.5f * d * float3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));
        voxel_size = 0.5f * d;

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

	    const uint2 output_counts = cube_index_2_mesh_output_counts [cube_index];
	    vertices_count = output_counts.x;
	    primitives_count = output_counts.y;

	    uint packed_offsets = 0;
	    const uint packed_counts = (primitives_count << 16) | vertices_count;
	    InterlockedAdd (s_packed_offsets, packed_counts, packed_offsets);

	    vertex_offset = packed_offsets & 0xFFFF;
	    primitive_offset = packed_offsets >> 16;
	}

	GroupMemoryBarrierWithGroupSync ();

	if (vox_idx == 0) {
	    const uint workgroup_vertex_count = s_packed_offsets & 0xFFFF;
	    const uint workgroup_primitive_count = s_packed_offsets >> 16;
	    SetMeshOutputCounts (workgroup_vertex_count, workgroup_primitive_count);
	}

	GroupMemoryBarrierWithGroupSync ();

    if (has_geometry) {
        int edge_mask = cube_index_2_edge_mask [cube_index];
        uint edge_to_local_idx [12]; 
        uint current_v_idx = 0;

        for (int i = 0; i < MAX_VERTS_PER_VOXEL; ++i) {
            bool active = (edge_mask & (1 << i)) != 0;
            if (!active) continue;

            const uint2 corner_indices = edge_corners [i];
            float3 position = interpolate_vertex (0.f, corners [corner_indices.x]
                , corners [corner_indices.y]
                , sdf_values [corner_indices.x]
                , sdf_values [corner_indices.y]);

            const float h = voxel_size * 0.01f;
            const float dx = eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position + float3 (h, 0, 0))
                           - eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position - float3 (h, 0, 0));
            const float dy = eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position + float3 (0, h, 0))
                           - eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position - float3 (0, h, 0));
            const float dz = eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position + float3 (0, 0, h))
                           - eval_local_voxel_sdf (sdf_values, voxel_min_pos, voxel_size, position - float3 (0, 0, h));
            const float3 norm = normalize (float3 (dx, dy, dz));

            MeshOutput v;
            v.clip_position  = mul (pc.view_proj, float4 (position, 1.0f));
            v.position       = float4 (position, 1.0f);
            v.normal         = float4 (norm, 1.0f);
            if (pc.color_leafs == 1u) {
                v.color = colors [brick_offset % COLORS_COUNT];
            } else if (pc.color_leafs == 2u) {
                v.color = voxel_size_color (voxel_size, pc.min_voxel_size, pc.max_voxel_size);
            } else {
                v.color = float4 (1.f);
            }

            uint vertex_index = vertex_offset + current_v_idx;
            edge_to_local_idx [i] = vertex_index;
            out_vertices [vertex_index] = v;
            current_v_idx += 1;
        }

        for (int i = 0; i < primitives_count; ++i) {
            const int base = cube_index * 16 + i * 3;
            uint e0 = cube_index_2_triangle_indices [base + 0];
            uint e1 = cube_index_2_triangle_indices [base + 1];
            uint e2 = cube_index_2_triangle_indices [base + 2];

            out_primitives [primitive_offset + i] = uint3 (
                edge_to_local_idx [e0]
                , edge_to_local_idx [e1]
                , edge_to_local_idx [e2]
                );
        }
    }
}

