// frustum_culling.slang

#include "shader_common.hpp"

static const float3 VOXEL_AXES [3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}
};

inline bool check_intervals_overlap (float min1, float max1, float min2, float max2) {
    return !(max1 < min2 || max2 < min1);
}

inline void project_aabb_cube_on_axis (float3 aabb_cube_min, float3 aabb_cube_max, float3 axis, out float min_proj, out float max_proj) {
    float3 center = (aabb_cube_min + aabb_cube_max) * 0.5f;
    float3 half_extents = (aabb_cube_max - aabb_cube_min) * 0.5f;

    float center_proj = dot (center, axis);
    float radius_proj = abs (axis.x * half_extents.x) + abs (axis.y * half_extents.y) + abs (axis.z * half_extents.z);

    min_proj = center_proj - radius_proj;
    max_proj = center_proj + radius_proj;
}

bool aabb_cube_frustum_intersects (ConstantBuffer <FrustumGeometry> frustum_cb, float3 voxel_min_corner, float voxel_size) {
    float3 voxel_max_corner = voxel_min_corner + float3 (voxel_size, voxel_size, voxel_size);

    for (int i = 0; i < 3; i++) {
        const float3 axis = VOXEL_AXES [i];

        float voxel_min_proj, voxel_max_proj;
        project_aabb_cube_on_axis (voxel_min_corner, voxel_max_corner, axis, voxel_min_proj, voxel_max_proj);

        float frustum_min_proj = dot (frustum_cb.vertices [0].xyz, axis);
        float frustum_max_proj = frustum_min_proj;
        for (int k = 1; k < 8; k++) {
            float proj = dot (frustum_cb.vertices [k].xyz, axis);
            if (proj < frustum_min_proj) frustum_min_proj = proj;
            if (proj > frustum_max_proj) frustum_max_proj = proj;
        }

        if (!check_intervals_overlap (voxel_min_proj, voxel_max_proj, frustum_min_proj, frustum_max_proj)) {
            return false;
        }
    }

    for (int i = 0; i < 6; i++) {
        const float3 axis = frustum_cb.normals [i].xyz;

        float voxel_min_proj, voxel_max_proj;
        project_aabb_cube_on_axis (voxel_min_corner, voxel_max_corner, axis, voxel_min_proj, voxel_max_proj);

        float frustum_min_proj = dot (frustum_cb.vertices [0].xyz, axis);
        float frustum_max_proj = frustum_min_proj;
        for (int k = 1; k < 8; k++) {
            float proj = dot (frustum_cb.vertices [k].xyz, axis);
            if (proj < frustum_min_proj) frustum_min_proj = proj;
            if (proj > frustum_max_proj) frustum_max_proj = proj;
        }

        if (!check_intervals_overlap (voxel_min_proj, voxel_max_proj, frustum_min_proj, frustum_max_proj)) {
            return false;
        }
    }

    return true;
}

