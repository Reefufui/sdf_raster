// task_generator.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

// [[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
// [[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
// [[vk::binding (2, 0)]] RWStructuredBuffer <LeafContext> g_active_leafs;
// [[vk::binding (3, 0)]] RWStructuredBuffer <uint> g_insufficent_mem_flag;
//
// [[vk::binding (0, 1)]] StructuredBuffer <uint2> edge_corners; // [12]
// [[vk::binding (1, 1)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
// [[vk::binding (2, 1)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
// [[vk::binding (3, 1)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

[[vk::binding (0, 0)]] RWStructuredBuffer <Vertex> vertices;
[[vk::binding (1, 0)]] RWStructuredBuffer <uint> indices;

[shader ("compute")]
[numthreads (1, 1, 1)]
void main (in uint3 group_id : SV_GroupID) {
    if (group_id.x == 0) {
        vertices [0] = Vertex(float4( 0.0f,  0.5f, 0.0f, 1.0f), float4(1.0f, 0.0f, 0.0f, 1.0f));
        vertices [1] = Vertex(float4(-0.5f, -0.5f, 0.0f, 1.0f), float4(0.0f, 1.0f, 0.0f, 1.0f));
        vertices [2] = Vertex(float4( 0.5f, -0.5f, 0.0f, 1.0f), float4(0.0f, 0.0f, 1.0f, 1.0f));
    }
}

