// task_generator.slang

#include "shader_common.hpp"

// [[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;
//
// [[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
// [[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;
//
// [[vk::binding (0, 1)]] RWStructuredBuffer <LeafContext> g_active_leafs;
// [[vk::binding (1, 1)]] RWStructuredBuffer <uint> g_insufficent_mem_flag;
//
// [[vk::binding (0, 2)]] StructuredBuffer <uint2> edge_corners; // [12]
// [[vk::binding (1, 2)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
// [[vk::binding (2, 2)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
// [[vk::binding (3, 2)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]
//
// groupshared TaskPayload s_payload;
// groupshared uint s_vertices_current_offset;
// groupshared uint s_triangles_current_offset;
//
// void add_leaf (NodeContext context, SdfOctreeNode leaf_node) {
//     context.cube_index = 0;
//
//     for (int i = 0; i < 8; ++i) {
//         if (leaf_node.values [i] < 0.0f) {
//             context.cube_index |= (1 << i);
//         }
//     }
//
//     if (context.cube_index == 0 || context.cube_index == 255) {
//         return;
//     }
//
//     const uint leaf_vertices_count = cube_index_2_mesh_output_counts [context.cube_index].x;
//     const uint leaf_triangles_count = cube_index_2_mesh_output_counts [context.cube_index].y;
//
//     if (s_payload.leaf_count < pc.max_count_per_task) {
//         g_active_leafs [s_payload.leaf_buffer_offset + s_payload.leaf_count].node_context = context;
//         g_active_leafs [s_payload.leaf_buffer_offset + s_payload.leaf_count].vertices_local_offset = s_vertices_current_offset;
//         g_active_leafs [s_payload.leaf_buffer_offset + s_payload.leaf_count].triangles_local_offset = s_triangles_current_offset;
//     } else {
//         g_insufficent_mem_flag [0] = 1;
//     }
//
//     s_payload.leaf_count += 1;
//     s_payload.vertices_count += leaf_vertices_count;
//     s_payload.triangles_count += leaf_triangles_count;
//     s_vertices_current_offset += leaf_vertices_count;
//     s_triangles_current_offset += leaf_triangles_count;
//
//     if (s_payload.leaf_count % MESH_WORKGROUP_SIZE == 0) {
//         s_vertices_current_offset = 0;
//         s_triangles_current_offset = 0;
//     }
// }
//
// void dfs_octree (NodeContext root) {
//     if (g_octree_data [root.node_index].offset == 0) {
//         add_leaf (root, g_octree_data [root.node_index]);
//         return;
//     }
//
//     int node_index_stack [MAX_OCTREE_DEPTH];
//     float3 coord_stack [MAX_OCTREE_DEPTH];
//     int path_stack [MAX_OCTREE_DEPTH];
//
//     node_index_stack [0] = root.node_index;
//     coord_stack [0] = float4 (root.min_corner_x, root.min_corner_y, root.min_corner_z, 1.f);
//     path_stack [1] = 0;
//
//     int level = 1; // starting with level 1
//     while (level > 0) {
//         int child_index = path_stack [level];
//         if (child_index == 8 || level >= pc.max_octree_depth) { // no more children
//             level -= 1;
//             continue;
//         }
//         path_stack [level] += 1;
//
//         SdfOctreeNode parent_node = g_octree_data [node_index_stack [level - 1]];
//         int child_node_index = parent_node.offset + child_index;
//         SdfOctreeNode child_node = g_octree_data [child_node_index];
//
//         float child_voxel_size = root.voxel_size / (1 << level);
//
//         float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
//         if (((child_index >> 0) & 1) > 0) child_corner_offset.x = child_voxel_size;
//         if (((child_index >> 1) & 1) > 0) child_corner_offset.y = child_voxel_size;
//         if (((child_index >> 2) & 1) > 0) child_corner_offset.z = child_voxel_size;
//
//         float3 child_coord = coord_stack [level - 1] + child_corner_offset;
//
//         if (child_node.offset == 0) { // leaf
//             NodeContext context;
//             context.voxel_size = child_voxel_size;
//             context.min_corner = child_coord;
//             context.node_index = child_node_index;
//             add_leaf (context, child_node);
//         } else { // internal node
//             node_index_stack [level] = child_node_index;
//             coord_stack [level] = child_coord;
//
//             level += 1;
//             path_stack [level] = 0;
//         }
//     }
// }
//
[shader ("amplification")]
[numthreads (1, 1, 1)]
void main (in uint3 group_id : SV_GroupID) {
//     s_payload.leaf_buffer_offset = pc.max_count_per_task * group_id.x;
//     s_payload.leaf_count = 0;
//     s_payload.triangles_count = 0;
//     s_payload.vertices_count = 0;
//     s_vertices_current_offset = 0;
//     s_triangles_current_offset = 0;
//
//     dfs_octree (g_roots [group_id.x]);
//
//     if (s_payload.leaf_count > 0) {
//         DispatchMesh ((s_payload.leaf_count + MESH_WORKGROUP_SIZE - 1) / MESH_WORKGROUP_SIZE, 1, 1, s_payload);
//     }
}

