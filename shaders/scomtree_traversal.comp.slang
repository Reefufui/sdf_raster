// scomtree_traversal.comp.slang

#include "shader_common.hpp"

#include "scomtree_utils.slang"
#include "frustum_culling.slang"
#include "occlusion_culling.slang"
#include "lod_utils.slang"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] ConstantBuffer <SComTreeHeader> g_scomtree_header;
[[vk::binding (1, 0)]] StructuredBuffer <uint> g_scomtree_nodes;
[[vk::binding (2, 0)]] StructuredBuffer <uint> g_scomtree_bricks;
[[vk::binding (3, 0)]] StructuredBuffer <int4> g_rotation_modifiers; // NOTE: 3 * 48
[[vk::binding (4, 0)]] StructuredBuffer <uint> g_rotation_add; // NOTE: 2304
[[vk::binding (5, 0)]] StructuredBuffer <SComTreeStackElement> g_roots;

[[vk::binding (0, 1)]] RWStructuredBuffer <uint> g_active_leaf_counter;
[[vk::binding (1, 1)]] RWStructuredBuffer <SComTreeBrickPayload> g_active_leafs;

[[vk::binding (0, 2)]] ConstantBuffer <FrustumGeometry> g_frustum_geometry;

[[vk::binding (0, 3)]] Sampler2D <float> g_depth_sampler;

[[vk::binding (0, 4)]] RWStructuredBuffer <LevelOfDetail> g_lod;

bool is_visible_node (bool frustum_culling, bool occlusion_culling, float3 node_coord, float node_size, out float max_dim) {
    if (frustum_culling) {
        if (!aabb_cube_frustum_intersects (g_frustum_geometry, node_coord, node_size)) {
            max_dim = 0.f;
            return false;
        }
    }

    if (occlusion_culling) {
        float3 child_corners [8];
        [unroll]
        for (int i = 0; i < 8; ++i) {
            child_corners [i].x = node_coord.x + (((i >> 0) & 1) * node_size);
            child_corners [i].y = node_coord.y + (((i >> 1) & 1) * node_size);
            child_corners [i].z = node_coord.z + (((i >> 2) & 1) * node_size);
        }

        if (voxel_occluded (g_depth_sampler, pc.prev_view_proj, child_corners, max_dim)) {
            return false;
        }
    }

    return true;
}

#define MAX_OCTREE_DEPTH 16
#define MAX_DIRECT_DESCEND_PATH 5
#define SCOM2_EPS 1e-6f

void next_child_or_pop (uint child, inout SComTreeStackElement cur, inout SComTreeStackElement stack [MAX_OCTREE_DEPTH], inout int top) {
    const uint next_child = child + 1;
    if (next_child >= 8) {
        cur = stack [top];
        top = top - 1;
    } else {
        cur.info = next_child | (cur.info & 0xFFFFFF00u);
    }
}

void push_self_for_next_child (uint child, inout SComTreeStackElement cur, inout SComTreeStackElement stack [MAX_OCTREE_DEPTH], inout int top) {
    const uint next_child = child + 1;
    if (next_child < 8) {
        cur.info = next_child | (cur.info & 0xFFFFFF00u);
        stack [++top] = cur; // NOTE: return parent we previously popped for other children
    }
}

void push_brick_payload (SComTreeBrickPayload payload) {
    uint active_leaf_offset = 0;
    InterlockedAdd (g_active_leaf_counter [0], 1, active_leaf_offset);

    if (active_leaf_offset >= pc.active_leafs_max_count) {
        return;
    }

    g_active_leafs [active_leaf_offset] = payload;
}

void p_size_desider (uint2 p_size, uint lod, out bool cull, out bool hit_lod) {
    const uint3 p = uint3 (p_size.x >> 16, p_size.x & 0xFFFF, p_size.y >> 16);
    const uint level_sz = p_size.y & 0xFFFF;

    const float node_size = 2.0f / float (level_sz);
    const float3 node_coord = float3 (-1.0f, -1.0f, -1.0f) + node_size * float3 (p);

    float max_dim;
    // bool frustum_culling = true; // TODO
    // bool occlusion_culling = true; // TODO
    bool frustum_culling = false; // TODO
    bool occlusion_culling = false; // TODO
    cull = !is_visible_node (frustum_culling, occlusion_culling, node_coord, node_size, max_dim);

    if (pc.lod_mode == LOD_MODE_PER_NODE) { // NOTE: per-node LOD mode
        const uint node_lod = calc_lod_for_node (node_coord, node_size, pc.subtree_root_level, pc.max_lod);
        hit_lod = (1u << (node_lod - 1u)) <= level_sz;
    } else {
        hit_lod = (1u << (lod - 1u)) <= level_sz;
    }
}

// NOTE: root expected to have children
void dfs_scomtree (SComTreeStackElement cur, uint lod) {
    int top = 0;
    SComTreeStackElement stack [16];
    stack [top] = cur;

    while (top >= 0) {
        uint child = cur.info & 0x7;

        uint child_has_data = 0u;
        uint child_is_leaf = 0u;
        uint child_link = 0u;
        uint rot_id = 0u;

        const uint2 child_node_p_size = (cur.p_size << 1) | uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);

        bool cull = false;
        bool hit_lod = false;
        uint node_lod_sz = 1u << (lod - 1);
        p_size_desider (child_node_p_size, lod, cull, hit_lod);

        if (!cull) {
            const uint3 voxel_pos = uint3 ((child & 4) >> 2, (child & 2) >> 1, child & 1); // (0,0,0)..(1,1,1)
            const uint child_n = uint (dot (g_rotation_modifiers [2 * 48 + cur.transform], int4 (int (voxel_pos.x), int (voxel_pos.y), int (voxel_pos.z), 1)));
            const uint child_n_mask = 1u << (2 * child_n);
            const uint children_types_mask = (cur.info >> g_scomtree_header.children_types_shift) & g_scomtree_header.children_types_mask;
            const uint active_children_mask = ((children_types_mask & 0x0000AAAAu) >> 1) | (children_types_mask & 0x00005555u);
            child_link = cur.links_offset + countbits (active_children_mask & (child_n_mask - 1));
            child_has_data = active_children_mask & child_n_mask;
            child_is_leaf = cur.info & (1u << (g_scomtree_header.children_types_shift + 2 * child_n));
            rot_id = cur.info >> g_scomtree_header.children_active_bits_shift;

            if (child_has_data > 0u && child_is_leaf == 0u && hit_lod) {
                uint link_data = g_scomtree_nodes [child_link];
                SdfDAGChildEdge ce = unpack_child_edge (g_scomtree_header, link_data, link_data);

                child_link = ce.child_offset + g_scomtree_header.links_offset;
                rot_id = g_rotation_add [rot_id * 48 + ce.rotation_id];
                child_is_leaf = 1u;
            }
        }

        if (child_has_data == 0u) {
            next_child_or_pop (child, cur, stack, top);
        } else if (child_is_leaf > 0u) {
            SComTreeBrickPayload payload;
            payload.p_size = child_node_p_size;
            payload.rot_link = (rot_id << 26) | child_link;
            push_brick_payload (payload);
            next_child_or_pop (child, cur, stack, top);
        } else {
            push_self_for_next_child (child, cur, stack, top);

            uint link_data = g_scomtree_nodes [child_link];
            SdfDAGChildEdge ce = unpack_child_edge (g_scomtree_header, link_data, link_data);

            uint node_data = g_scomtree_nodes [ce.child_offset];
            NodeHeadUnpacked node = unpack_node_head (g_scomtree_header, node_data, node_data);

            const uint init_child = 0u;

            const uint rot_idx = g_rotation_add [rot_id * 48 + ce.rotation_id];
            cur.links_offset = ce.child_offset + g_scomtree_header.links_offset + (node.base_type == SCOM2_CHILD_EMPTY ? 0 : 1);
            cur.transform = rot_idx;
            cur.info = (rot_idx << 24) | (node.children_types << 8) | init_child;
            cur.p_size = child_node_p_size;
        }
    }
}

bool descend_path (uint path [MAX_DIRECT_DESCEND_PATH], inout SComTreeStackElement cur, uint lod, out bool force_process_voxel) {
    force_process_voxel = true;
    const int direct_root_level = pc.subtree_root_level;

    // cur.info = path [0] | (cur.info & 0xFFFFFF00u);

    [unroll]
    for (uint direct_level = 0; direct_level < MAX_DIRECT_DESCEND_PATH; ++direct_level) {
    }

    force_process_voxel = direct_root_level + MAX_DIRECT_DESCEND_PATH >= lod;
    return true;
}

[shader ("compute")]
[numthreads (1, 1, 1)]
void main (uint3 dti :  SV_DispatchThreadID, uint3 gi : SV_GroupID, uint3 gti : SV_GroupThreadID) {
    const uint lod = (pc.lod_mode == LOD_MODE_FIXED) ? pc.fixed_lod
        : (pc.lod_mode == LOD_MODE_PER_TREE) ? calc_lod_for_node (pc.root_center.xyz - 0.5f * pc.max_voxel_size, pc.max_voxel_size, pc.subtree_root_level, pc.max_lod)
        : uint (MAX_OCTREE_DEPTH);

    SComTreeStackElement cur = g_roots [gi.z];
    dfs_scomtree (cur, lod);
}

