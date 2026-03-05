// mesh_sphere.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;

[[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 2)]] RWStructuredBuffer <uint> g_active_leaf_counter; // NOTE: unused by this shader (probably should move it to another ds)
[[vk::binding (1, 2)]] RWStructuredBuffer <NodeContext> g_active_leafs;

struct MeshOutput {
    float4 position : SV_Position;
    Vertex vertex;
};

#define MAX_VERTS_PER_WORKGROUP (VOXELS_PER_MESH_WORKGROUP * 12)
#define MAX_PRIMS_PER_WORKGROUP (VOXELS_PER_MESH_WORKGROUP * 5)
groupshared MeshOutput s_vertices [MAX_VERTS_PER_WORKGROUP];
groupshared uint3 s_indices [MAX_PRIMS_PER_WORKGROUP];

groupshared uint s_vertex_count;
groupshared uint s_primitive_count;

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (VOXELS_PER_MESH_WORKGROUP, 1, 1)]
void main (
    uint3 gi : SV_GroupID,
    uint3 gti : SV_GroupThreadID,
    uint3 dti : SV_DispatchThreadID,
    OutputVertices <MeshOutput, MAX_LEAF_VERTS> out_verts,
    OutputIndices <uint3, MAX_LEAF_PRIMS> out_indices) {

    if (gti.x == 0) {
        s_vertex_count = 0;
        s_primitive_count = 0;
    }
    GroupMemoryBarrierWithGroupSync ();

	if (dti.x >= g_active_leaf_counter [0]) return;

    for (uint i = gti.x; i < VOXELS_PER_MESH_WORKGROUP; ++i) {
        uint voxel_global_index = gi.x * VOXELS_PER_MESH_WORKGROUP + i;

        uint2 output_counts = cube_index_2_mesh_output_counts [0];

        uint vertex_offset = 0;
        uint primitive_offset = 0;
        InterlockedAdd (s_vertex_count, output_counts.x, vertex_offset);
        InterlockedAdd (s_primitive_count, output_counts.y, primitive_offset);
    }

    GroupMemoryBarrierWithGroupSync ();

    if (gti.x == 0) {
        SetMeshOutputCounts (s_vertex_count, s_primitive_count);

        for (uint j = 0; j < s_vertex_count; ++j) {
            out_verts [j] = s_vertices [j];
        }
        for(uint k = 0; k < s_primitive_count; ++k) {
            out_indices [k] = s_indices [k];
        }
    }

    SetMeshOutputCounts (3, 1);

    out_verts [0].position = mul (pc.view_proj, float4 (-0.5, 0.5, 0, 1));
    out_verts [1].position = mul (pc.view_proj, float4 (0.5,  0.5, 0, 1));
    out_verts [2].position = mul (pc.view_proj, float4 (0, -0.5, 0, 1));

    Vertex vertex;
    out_verts [0].vertex.position = float4 (-0.5, 0.5, 0, 1);
    out_verts [1].vertex.position = float4 (0.5,  0.5, 0, 1);
    out_verts [2].vertex.position = float4 (0, -0.5, 0, 1);
    out_verts [0].vertex.color = float4 (1, 0, 0, 1);
    out_verts [1].vertex.color = float4 (0, 1, 0, 1);
    out_verts [2].vertex.color = float4 (0, 1, 1, 1);

    out_indices [0] = uint3 (0, 1, 2);
}

