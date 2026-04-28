// engine/utils/frustum_culling.cpp
#include "frustum_culling.hpp"

#include <LiteMath.h>

#include <thread>

static const LiteMath::float3 VOXEL_AXES [3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f}
};

inline bool check_intervals_overlap (float min1, float max1, float min2, float max2) {
    return !(max1 < min2 || max2 < min1);
}

inline void project_aabb_cube_on_axis (LiteMath::float3 aabb_cube_min, LiteMath::float3 aabb_cube_max, LiteMath::float3 axis, float& min_proj, float& max_proj) {
    LiteMath::float3 center = (aabb_cube_min + aabb_cube_max) * 0.5f;
    LiteMath::float3 half_extents = (aabb_cube_max - aabb_cube_min) * 0.5f;

    float center_proj = LiteMath::dot (center, axis);
    float radius_proj = LiteMath::abs (axis.x * half_extents.x) + LiteMath::abs (axis.y * half_extents.y) + LiteMath::abs (axis.z * half_extents.z);

    min_proj = center_proj - radius_proj;
    max_proj = center_proj + radius_proj;
}

bool aabb_cube_frustum_intersects (const FrustumGeometry& frustum_cb, LiteMath::float3 voxel_min_corner, float voxel_size) {
    LiteMath::float3 voxel_max_corner = voxel_min_corner + LiteMath::float3 (voxel_size, voxel_size, voxel_size);

    for (int i = 0; i < 3; i++) {
        const LiteMath::float3 axis = VOXEL_AXES [i];

        float voxel_min_proj, voxel_max_proj;
        project_aabb_cube_on_axis (voxel_min_corner, voxel_max_corner, axis, voxel_min_proj, voxel_max_proj);

        float frustum_min_proj = LiteMath::dot (LiteMath::to_float3 (frustum_cb.vertices [0]), axis);
        float frustum_max_proj = frustum_min_proj;
        for (int k = 1; k < 8; k++) {
            float proj = LiteMath::dot (LiteMath::to_float3 (frustum_cb.vertices [k]), axis);
            if (proj < frustum_min_proj) frustum_min_proj = proj;
            if (proj > frustum_max_proj) frustum_max_proj = proj;
        }

        if (!check_intervals_overlap (voxel_min_proj, voxel_max_proj, frustum_min_proj, frustum_max_proj)) {
            return false;
        }
    }

    for (int i = 0; i < 6; i++) {
        const LiteMath::float3 axis = LiteMath::to_float3 (frustum_cb.normals [i]);

        float voxel_min_proj, voxel_max_proj;
        project_aabb_cube_on_axis (voxel_min_corner, voxel_max_corner, axis, voxel_min_proj, voxel_max_proj);

        float frustum_min_proj = LiteMath::dot (LiteMath::to_float3 (frustum_cb.vertices [0]), axis);
        float frustum_max_proj = frustum_min_proj;
        for (int k = 1; k < 8; k++) {
            float proj = LiteMath::dot (LiteMath::to_float3 (frustum_cb.vertices [k]), axis);
            if (proj < frustum_min_proj) frustum_min_proj = proj;
            if (proj > frustum_max_proj) frustum_max_proj = proj;
        }

        if (!check_intervals_overlap (voxel_min_proj, voxel_max_proj, frustum_min_proj, frustum_max_proj)) {
            return false;
        }
    }

    return true;
}


void frustum_culling (const std::vector <NodeContext>& nodes, const FrustumGeometry& frustum, std::vector <NodeContext>& result) {
    result.clear ();

    for (const auto& node : nodes) {
        LiteMath::float3 min_corner = {node.min_corner_x, node.min_corner_y, node.min_corner_z};
        if (aabb_cube_frustum_intersects (frustum, min_corner, node.voxel_size)) {
            result.push_back (node);
        }
    }
}

void frustum_culling (const std::vector <SComTreeStackElement>& nodes, const FrustumGeometry& /*frustum*/, std::vector <SComTreeStackElement>& result) {
    result.clear ();

    /*
    for (const auto& node : nodes) {
        LiteMath::float3 min_corner = {node.min_corner_x, node.min_corner_y, node.min_corner_z};
        if (aabb_cube_frustum_intersects (frustum, min_corner, node.voxel_size)) {
            result.push_back (node);
        }
    }
    */

    result = nodes;
}


