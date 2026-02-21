

// false if fully occluded
bool voxel_occluded (const float3 coord, const float voxel_size) {
    float3 voxel_corners [8];
    for (int i = 0; i < 8; ++i) {
        float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) == 1) corner_offset.x = voxel_size;
        if (((i >> 1) & 1) == 1) corner_offset.y = voxel_size;
        if (((i >> 2) & 1) == 1) corner_offset.z = voxel_size;
        voxel_corners [i] = coord + corner_offset;
    }

    for (int i = 0; i < 8; ++i) {
        float4 proj = mul (pc.prev_view_proj, float4 (voxel_corners [i], 1.0f));
        voxel_corners [i] = float3 (proj.x / proj.w, proj.y / proj.w, proj.z / proj.w);
    }

    // TODO: occlusion culling

    // All tests passed, volumes intersect
    return false;
}

