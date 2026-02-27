// flatshadable_voxel.slang

static const float3 VoxelCubeNormals [] = {
    float3 ( 0.0f,  0.0f, -1.0f),
    float3 ( 0.0f,  0.0f,  1.0f),
    float3 (-1.0f,  0.0f,  0.0f),
    float3 ( 1.0f,  0.0f,  0.0f),
    float3 ( 0.0f, -1.0f,  0.0f),
    float3 ( 0.0f,  1.0f,  0.0f)
};

static const float3 VoxelCubeVertexOffsets [] = {
    float3 (0.0f, 0.0f, 0.0f),
    float3 (1.0f, 0.0f, 0.0f),
    float3 (1.0f, 1.0f, 0.0f),
    float3 (0.0f, 1.0f, 0.0f),
    float3 (0.0f, 0.0f, 1.0f),
    float3 (1.0f, 0.0f, 1.0f),
    float3 (1.0f, 1.0f, 1.0f),
    float3 (0.0f, 1.0f, 1.0f)
};

static const uint VoxelCubeFaceVertexIndices [6][4] = {
    // (-Z)
    {0, 3, 2, 1},

    // (+Z)
    {4, 5, 6, 7},

    // (-X)
    {0, 4, 7, 3},

    // (+X)
    {1, 2, 6, 5},

    // (-Y)
    {0, 1, 5, 4},

    // (+Y)
    {3, 7, 6, 2}
};

// VK_FRONT_FACE_COUNTER_CLOCKWISE
static const uint VoxelCubeTriangulationIndices [6] = {
    0, 2, 1,
    0, 3, 2
};

void flatshadable_voxel (NodeContext node_data, uint thread_id, float4 color) {
    uint base_vertex_offset = thread_id * 24;
    uint base_index_offset = thread_id * 36;

    float4 min_corner = float4 (node_data.min_corner_x, node_data.min_corner_y, node_data.min_corner_z, 0.0f);
    float voxel_size = node_data.voxel_size;

    for (uint face_idx = 0; face_idx < 6; ++face_idx) {
        float3 face_normal = VoxelCubeNormals [face_idx];
        uint current_face_base_vertex_idx = base_vertex_offset + face_idx * 4;

        for (uint i = 0; i < 4; ++i) {
            uint offset_idx = VoxelCubeFaceVertexIndices [face_idx][i];
            float3 vertex_offset = VoxelCubeVertexOffsets [offset_idx];
            float4 world_pos = min_corner + float4 (vertex_offset * voxel_size, 1.0f);

            float4 transformed_pos = mul (pc.view_proj, world_pos);
            g_vertices [current_face_base_vertex_idx + i].position = transformed_pos;
            g_vertices [current_face_base_vertex_idx + i].normal = float4 (face_normal, 0.0f);
            g_vertices [current_face_base_vertex_idx + i].color = color;
        }

        uint current_face_base_index_idx = base_index_offset + face_idx * 6;
        g_indices [current_face_base_index_idx + 0] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [0];
        g_indices [current_face_base_index_idx + 1] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [1];
        g_indices [current_face_base_index_idx + 2] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [2];
        g_indices [current_face_base_index_idx + 3] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [3];
        g_indices [current_face_base_index_idx + 4] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [4];
        g_indices [current_face_base_index_idx + 5] = current_face_base_vertex_idx + VoxelCubeTriangulationIndices [5];
    }
}

