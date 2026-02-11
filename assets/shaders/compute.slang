// task_generator.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] StructuredBuffer <SdfOctreeNode> g_octree_data;
[[vk::binding (1, 0)]] StructuredBuffer <NodeContext> g_roots;

[[vk::binding (0, 1)]] RWStructuredBuffer <Vertex> vertices;
[[vk::binding (1, 1)]] RWStructuredBuffer <uint> indices;
[[vk::binding (2, 1)]] RWStructuredBuffer <uint> g_insufficent_mem_flag;

[[vk::binding (0, 2)]] StructuredBuffer <uint2> edge_corners; // [12]
[[vk::binding (1, 2)]] StructuredBuffer <int> cube_index_2_edge_mask; // [256]
[[vk::binding (2, 2)]] StructuredBuffer <int> cube_index_2_triangle_indices; // [256][16]
[[vk::binding (3, 2)]] StructuredBuffer <uint2> cube_index_2_mesh_output_counts; // [256]

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

groupshared uint s_vertices_count;
groupshared uint s_indices_count;

void process_voxel (NodeContext context, SdfOctreeNode leaf_node) {
    context.cube_index = 0;

    for (int i = 0; i < 8; ++i) {
        if (leaf_node.values [i] < 0.0f) {
            context.cube_index |= (1 << i);
        }
    }

    if (context.cube_index == 0 || context.cube_index == 255) {
        return;
    }

    const uint leaf_vertices_count = cube_index_2_mesh_output_counts [context.cube_index].x;
    const uint leaf_triangles_count = cube_index_2_mesh_output_counts [context.cube_index].y;

    if (s_vertices_count + leaf_vertices_count > pc.max_count_per_task) {
        return;
        // g_insufficent_mem_flag [0] = 1;
    }

    const float3 voxel_size_modifier = {context.voxel_size};
    float3 corners [8];

    for (int i = 0; i < 8; ++i) {
        float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) == 1) corner_offset.x = context.voxel_size;
        if (((i >> 1) & 1) == 1) corner_offset.y = context.voxel_size;
        if (((i >> 2) & 1) == 1) corner_offset.z = context.voxel_size;
        corners [i] = context.min_corner + corner_offset;
    }

    uint triangles_count = 0;
    for (; triangles_count < MAX_LEAF_PRIMS; ++triangles_count) {
        if (cube_index_2_triangle_indices [context.cube_index * 16 + triangles_count * 3] == -1) break;
    }

    for (int i = 0; i < triangles_count; ++i) {
        const int base = context.cube_index * 16 + i * 3;
        indices [i * 3 + 0] = s_vertices_count + cube_index_2_triangle_indices [base + 0];
        indices [i * 3 + 1] = s_vertices_count + cube_index_2_triangle_indices [base + 1];
        indices [i * 3 + 2] = s_vertices_count + cube_index_2_triangle_indices [base + 2];
    }

    const int edge_mask = cube_index_2_edge_mask [context.cube_index]; // non-zero because of check in task shader
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
        vertex.color = colors [0];
        vertices [s_vertices_count + i] = vertex;

        edge_bit <<= 1;
    }

    s_vertices_count += leaf_vertices_count;
    s_indices_count += leaf_triangles_count;
}

void dfs_octree (NodeContext root) {
    if (g_octree_data [root.node_index].offset == 0) {
        process_voxel (root, g_octree_data [root.node_index]);
        return;
    }

    int node_index_stack [MAX_OCTREE_DEPTH];
    float3 coord_stack [MAX_OCTREE_DEPTH];
    int path_stack [MAX_OCTREE_DEPTH];

    node_index_stack [0] = root.node_index;
    coord_stack [0] = root.min_corner;
    path_stack [1] = 0;

    int level = 1; // starting with level 1
    while (level > 0) {
        int child_index = path_stack [level];
        if (child_index == 8 || level >= pc.max_octree_depth) { // no more children
            level -= 1;
            continue;
        }
        path_stack [level] += 1;

        SdfOctreeNode parent_node = g_octree_data [node_index_stack [level - 1]];
        int child_node_index = parent_node.offset + child_index;
        SdfOctreeNode child_node = g_octree_data [child_node_index];

        float child_voxel_size = root.voxel_size / (1 << level);

        float3 child_corner_offset = {0.0f, 0.0f, 0.0f};
        if (((child_index >> 0) & 1) > 0) child_corner_offset.x = child_voxel_size;
        if (((child_index >> 1) & 1) > 0) child_corner_offset.y = child_voxel_size;
        if (((child_index >> 2) & 1) > 0) child_corner_offset.z = child_voxel_size;

        float3 child_coord = coord_stack [level - 1] + child_corner_offset;

        if (child_node.offset == 0) { // leaf
            NodeContext context;
            context.voxel_size = child_voxel_size;
            context.min_corner = child_coord;
            context.node_index = child_node_index;
            process_voxel (context, child_node);
        } else { // internal node
            node_index_stack [level] = child_node_index;
            coord_stack [level] = child_coord;

            level += 1;
            path_stack [level] = 0;
        }
    }
}

[shader ("compute")]
[numthreads (1, 1, 1)]
void main (in uint3 group_id : SV_GroupID) {
    s_vertices_count = 0;
    s_indices_count = 0;
    if (group_id.x == 0) {
        dfs_octree (g_roots [group_id.x]);
        // vertices [0] = Vertex(float4( 0.0f,  0.5f, 0.0f, 1.0f), float4(1.0f, 0.0f, 0.0f, 1.0f));
        // vertices [1] = Vertex(float4(-0.5f, -0.5f, 0.0f, 1.0f), float4(0.0f, 1.0f, 0.0f, 1.0f));
        // vertices [2] = Vertex(float4( 0.5f, -0.5f, 0.0f, 1.0f), float4(0.0f, 0.0f, 1.0f, 1.0f));
        // indices [0] = 0;
        // indices [1] = 1;
        // indices [2] = 2;
    }
}

