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

bool is_visible_node (int level, float3 node_coord, float node_size, out float max_dim) {
    if (level <= pc.frustum_culling_level) {
        if (!aabb_cube_frustum_intersects (g_frustum_geometry, node_coord, node_size)) {
            max_dim = 0.f;
            return false;
        }
    }

    if (level <= pc.occlusion_culling_level) {
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

// NOTE: root expected to have children
void dfs_scomtree (SComTreeStackElement cur, uint lod) {
    const uint target_level_sz = 1u << lod;

    SComTreeStackElement stack [16];

    int top = 0;

    stack [0] = cur;

    while (top >= 0) {
        const uint child = cur.info & 0x7;

        const uint3 p = uint3 (cur.p_size.x >> 16, cur.p_size.x & 0xFFFF, cur.p_size.y >> 16);
        const uint level_sz = cur.p_size.y & 0xFFFF;

        if (bool (pc.frustum_culling_level) || bool (pc.occlusion_culling_level)) {
            const float node_size = 2.0f / float (level_sz);
            const float3 node_coord = float3 (-1.0f, -1.0f, -1.0f) + node_size * float3 (p);

            const int current_level = pc.subtree_root_level + top;
            float max_dim;

            if (!is_visible_node (current_level, node_coord, node_size, max_dim)) {
                const uint next_child = child + 1;
                if (next_child >= 8) {
                    cur = stack [top--];
                } else {
                    cur.info = next_child | (cur.info & 0xFFFFFF00u);
                }
                continue; 
            }
        }

        const uint3 voxel_pos = uint3 ((child & 4) >> 2, (child & 2) >> 1, child & 1); // (0,0,0)..(1,1,1)
        const uint child_n = uint (dot (g_rotation_modifiers [2 * 48 + cur.transform]
            , int4 (int (voxel_pos.x), int (voxel_pos.y), int (voxel_pos.z), 1)));
        const uint child_n_mask = 1u << (2 * child_n);

        const uint children_types_mask = (cur.info >> g_scomtree_header.children_types_shift) & g_scomtree_header.children_types_mask;
        const uint active_children_mask = ((children_types_mask & 0x0000AAAAu) >> 1) | (children_types_mask & 0x00005555u);
        const uint child_link = cur.links_offset + countbits (active_children_mask & (child_n_mask - 1));
        const uint child_has_data = active_children_mask & child_n_mask;
        const uint child_is_leaf = cur.info & (1u << (g_scomtree_header.children_types_shift + 2 * child_n));
        const uint rot_id = cur.info >> g_scomtree_header.children_active_bits_shift;

        const uint node_target_size = target_level_sz >> 1;

        if (child_has_data == 0) {
            const uint next_child = child + 1;
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        // } else if (level_sz == target_level_sz) {
        } else if (child_is_leaf > 0) {
            SComTreeBrickPayload payload;
            payload.p_size = (cur.p_size << 1) | uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
            payload.rot_link = (rot_id << 26) | child_link;

            uint active_leaf_offset = 0;
            InterlockedAdd (g_active_leaf_counter [0], 1, active_leaf_offset);

            if (active_leaf_offset >= pc.active_leafs_max_count) {
                return;
            }

            g_active_leafs [active_leaf_offset] = payload;

            const uint next_child = child + 1;
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else {
            const uint next_child = child + 1;
            if (next_child < 8) {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
                stack [++top] = cur; // NOTE: return parent we previously popped for other children
            }

            uint link_data = g_scomtree_nodes [child_link];
            SdfDAGChildEdge ce = unpack_child_edge (g_scomtree_header, link_data, link_data);

            uint node_data = g_scomtree_nodes [ce.child_offset];
            NodeHeadUnpacked node = unpack_node_head (g_scomtree_header, node_data, node_data);

            uint rot_idx = g_rotation_add [rot_id * 48 + ce.rotation_id];
            cur.links_offset = ce.child_offset + g_scomtree_header.links_offset + (node.base_type == SCOM2_CHILD_EMPTY ? 0 : 1);
            cur.transform = rot_idx;
            cur.info = (rot_idx << 24) | (node.children_types << 8);
            cur.p_size = (cur.p_size << 1) | uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
        }
    }
}

[shader ("compute")]
[numthreads (1, 1, 1)]
void main (uint3 dti :  SV_DispatchThreadID, uint3 gi : SV_GroupID, uint3 gti : SV_GroupThreadID) {
    uint lod = get_global_lod ();

    SComTreeStackElement cur = g_roots [gi.z];
    dfs_scomtree (cur, lod);
}

