// lod_utils.slang

#include "shader_common.hpp"

float distance_to_aabb (float3 p, float3 aabb_min, float3 aabb_max) {
    float3 q = max (max (aabb_min - p, 0.0f), p - aabb_max);
    return length (q);
}

uint calc_lod_for_node (float3 node_center, float node_size, uint subtree_root_level, uint max_lod) {
    const float half_size = node_size * 0.5f;
    const float3 node_min = node_center - half_size;
    const float3 node_max = node_center + half_size;

    const float distance = max (distance_to_aabb (pc.camera_pos.xyz, node_min, node_max), 1e-5f);
    const float tan_half_fov = tan (pc.fov_y * 0.5f);
    const float projected_pixels = node_size * float (pc.screen_height)
        / (2.0f * distance * tan_half_fov);

    const float lod_threshold_pixels = pc.lod_threshold_pixels;
    uint desired_depth = subtree_root_level;
    if (projected_pixels > lod_threshold_pixels) {
        desired_depth += uint (ceil (log2 (projected_pixels / lod_threshold_pixels)));
    }
    return min (desired_depth, max_lod);
}

uint get_global_lod () {
    return calc_lod_for_node (pc.root_center.xyz, pc.max_voxel_size, pc.subtree_root_level, pc.max_lod);
}
