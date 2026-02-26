static const float3 voxel_axes [3] = {
    {1.f, 0.f, 0.f},
    {0.f, 1.f, 0.f},
    {0.f, 0.f, 1.f}
};

bool test_axis (const float3 corners [8], const float3 axis) {
    float voxel_min_proj = dot (corners [0], axis);
    float voxel_max_proj = voxel_min_proj;

    for (int i = 1; i < 8; i++) {
        float proj = dot (corners [i], axis);
        if (proj < voxel_min_proj) voxel_min_proj = proj;
        if (proj > voxel_max_proj) voxel_max_proj = proj;
    }

    float frustum_min_proj = dot (frustum_geometry.vertices [0].xyz, axis);
    float frustum_max_proj = frustum_min_proj;

    for (int i = 1; i < 8; i++) {
        float proj = dot (frustum_geometry.vertices [i].xyz, axis);
        if (proj < frustum_min_proj) frustum_min_proj = proj;
        if (proj > frustum_max_proj) frustum_max_proj = proj;
    }

    return !(voxel_max_proj < frustum_min_proj || frustum_max_proj < voxel_min_proj);
}

// false if fully outside, true if inside or intersects
bool voxel_frustum_intersects (const float3 voxel_corners [8]) {
    // Test voxel axes
    for (int i = 0; i < 3; i++) {
        if (!test_axis (voxel_corners, voxel_axes [i])) return false;
    }

    // Test frustum face normals
    for (int i = 0; i < 6; i++) {
        if (!test_axis (voxel_corners, frustum_geometry.normals [i].xyz)) return false;
    }

    // Test cross product axes between box axes and frustum edges
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 12; j++) {
            float3 axis = cross (voxel_axes [i], frustum_geometry.edges [j].xyz);
            if (!test_axis (voxel_corners, axis)) return false;
        }
    }

    // All tests passed, volumes intersect
    return true;
}

