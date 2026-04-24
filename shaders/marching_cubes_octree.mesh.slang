// marching_cubes_octree.mesh.slang

#include "shader_common.hpp"

#include "colors.slang"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 2)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 2)]] RWStructuredBuffer <NodeContext> g_active_leafs;

[[vk::binding (0, 3)]] RWStructuredBuffer <LevelOfDetail> g_lod;

struct MeshOutput {
    float4 clip_position  : SV_Position;
    float4 position       : TEXCOORD0;
    float4 normal         : TEXCOORD1;
    float4 color          : COLOR0;
};

#define MAX_VERTS_PER_VOXEL 12
#define MAX_PRIMS_PER_VOXEL 4
#define MAX_VERTS_PER_WORKGROUP (VOXELS_PER_MESH_WORKGROUP * MAX_VERTS_PER_VOXEL)
#define MAX_PRIMS_PER_WORKGROUP (VOXELS_PER_MESH_WORKGROUP * MAX_PRIMS_PER_VOXEL)
groupshared MeshOutput s_vertices [MAX_VERTS_PER_WORKGROUP];
groupshared uint3 s_indices [MAX_PRIMS_PER_WORKGROUP];

// least 16 bit: vertex count
// most 16 bit: primitive count
groupshared uint s_packed_offsets;

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

void generate_geometry (const NodeContext voxel, const uint vertex_offset, const uint primitive_offset) {
    const float3 min_corner = {voxel.min_corner_x, voxel.min_corner_y, voxel.min_corner_z};

    float3 corners [8];
    for (int i = 0; i < 8; ++i) {
        corners [i] = min_corner + float3 (
            (float)((i >> 0) & 1)
            , (float)((i >> 1) & 1)
            , (float)((i >> 2) & 1)
        ) * voxel.voxel_size;
    }

    const uint triangles_count = cube_index_2_mesh_output_counts [voxel.cube_index].y;

    const SdfOctreeNode leaf_node = g_octree_data [voxel.node_index];
    const int edge_mask = cube_index_2_edge_mask [voxel.cube_index];
    int edge_bit = 1;

    uint strides [MAX_VERTS_PER_VOXEL];
    uint vertex_index = 0;
    uint vertex_skipped = 0;
    for (int i = 0; i < MAX_VERTS_PER_VOXEL; ++i) {
        if ((edge_mask & edge_bit) == 0) {
            vertex_skipped += 1;
            edge_bit <<= 1;
            continue;
        }

        const uint2 corner_indices = edge_corners [i];
        float3 position = interpolate (corners [corner_indices.x]
                     , corners [corner_indices.y]
                     , leaf_node.values [corner_indices.x]
                     , leaf_node.values [corner_indices.y]);


        MeshOutput v;
        v.clip_position  = mul (pc.view_proj, float4 (position, 1.0f));
        v.position       = float4 (position, 1.0f);
        v.normal         = float4 (sample_normal (position), 1.0f);
        v.color          = (pc.color_leafs == 1u) ? colors [voxel.node_index % COLORS_COUNT] : float4 (1.f);

        s_vertices [vertex_offset + vertex_index] = v;

        vertex_index += 1;
        strides [i] = vertex_skipped;
        edge_bit <<= 1;
    }

    for (int i = 0; i < triangles_count; ++i) {
        const int base = voxel.cube_index * 16 + i * 3;
        const uint3 t = uint3 (cube_index_2_triangle_indices [base + 0] , cube_index_2_triangle_indices [base + 1] , cube_index_2_triangle_indices [base + 2]);
        const uint3 stride = uint3 (strides [t.x], strides [t.y], strides [t.z]);
        s_indices [primitive_offset + i] = uint3 (vertex_offset) + t - stride;
    }
}

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (VOXELS_PER_MESH_WORKGROUP, 1, 1)]
void main (
    uint3 gi : SV_GroupID,
    uint3 gti : SV_GroupThreadID,
    uint3 dti : SV_DispatchThreadID,
    OutputVertices <MeshOutput, MAX_VERTS_PER_WORKGROUP> out_verts,
    OutputIndices <uint3, MAX_PRIMS_PER_WORKGROUP> out_indices) {

	if (gti.x == 0) {
	    s_packed_offsets = 0;
	}
	if (dti.x >= g_active_leaf_counter [0]) return;

	GroupMemoryBarrierWithGroupSync ();

	NodeContext voxel = g_active_leafs [dti.x];

	const uint2 output_counts = cube_index_2_mesh_output_counts [voxel.cube_index];
	const uint packed_counts = (output_counts.y << 16) | output_counts.x;
	uint packed_offsets = 0;
	InterlockedAdd (s_packed_offsets, packed_counts, packed_offsets);

	const uint vertex_offset = packed_offsets & 0xFFFF;
	const uint primitive_offset = packed_offsets >> 16;

	generate_geometry (voxel, vertex_offset, primitive_offset);

	GroupMemoryBarrierWithGroupSync ();
	if (gti.x == 0) {
	    const uint workgroup_vertex_count = s_packed_offsets & 0xFFFF;
	    const uint workgroup_primitive_count = s_packed_offsets >> 16;
	    SetMeshOutputCounts (workgroup_vertex_count, workgroup_primitive_count);
	}
	GroupMemoryBarrierWithGroupSync ();

	for (uint j = 0; j < output_counts.x; ++j) {
	    out_verts [vertex_offset + j] = s_vertices [vertex_offset + j];
	}
	for(uint k = 0; k < output_counts.y; ++k) {
	    out_indices [primitive_offset + k] = s_indices [primitive_offset + k];
	}

    // #include "mesh_shader_debug.inl.slang"
}

