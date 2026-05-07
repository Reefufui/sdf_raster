// lod_utils.slang

#include "shader_common.hpp"

#define LOD_MODE_FIXED 0
#define LOD_MODE_PER_TREE 1
#define LOD_MODE_PER_NODE 2

float distance_to_aabb (float3 p, float3 aabb_min, float3 aabb_max) {
    float3 q = max (max (aabb_min - p, 0.0f), p - aabb_max);
    return length (q);
}

uint calc_lod_for_node (float3 node_min, float node_size, uint subtree_root_level, uint max_lod) {
    const float3 node_max = node_min + float3 (node_size);

    const float distance = max (distance_to_aabb (pc.camera_pos.xyz, node_min, node_max), 1e-5f);
    const float tan_half_fov = tan (pc.fov_y * 0.5f);
    const float projected_pixels = node_size * float (pc.screen_height)
        / (2.0f * distance * tan_half_fov);

    const float lod_threshold_pixels = pc.lod_threshold_pixels;
    float desired_depth = float (subtree_root_level);
    if (projected_pixels > lod_threshold_pixels) {
        desired_depth += log2 (projected_pixels / lod_threshold_pixels) * pc.lod_aggressivity;
    }
    return uint (clamp (desired_depth, float (pc.min_lod), float (max_lod)));
}

uint get_global_lod () {
    return calc_lod_for_node (pc.root_center.xyz - 0.5f * pc.max_voxel_size, pc.max_voxel_size, pc.subtree_root_level, pc.max_lod);
}
