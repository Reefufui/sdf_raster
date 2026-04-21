#include "scenes/scomtree/scomtree.hpp"

#include "frustum_culling.hpp"
#include "logger.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "scenes/scomtree/defs.hpp"
#include "scenes/scomtree/rotation_lookup_tables.hpp"
#include "scenes/scomtree/utils.hpp"
#include "shader_common.hpp"

#include <fstream>

namespace sdf_raster {

bool SComTreeScene::load (const std::filesystem::path& path) {
    std::ifstream fs (path, std::ios::binary);

    uint32_t magic_number = 0;
    uint32_t version = 0;
    uint32_t num_nodes = 0;
    uint32_t num_bricks = 0;
    uint32_t vc_count = 0;
    uint32_t pc_count = 0;

    fs.read ((char *)&magic_number, sizeof (uint32_t));

    if (magic_number != SCOM2_MAGIC_NUMBER) {
        fs.close ();
        LOG_ERROR ("Legacy scomtree is not supported.");
        return false;
    }

    fs.read ((char *)&version, sizeof (uint32_t));

    if (version != SCOM2_VERSION) {
        fs.close ();
        LOG_ERROR ("SComTree version mismatch (save is version {}, current version is {})", version, SCOM2_VERSION);
        return false;
    }

    fs.read ((char *)&num_nodes, sizeof (uint32_t));
    fs.read ((char *)&num_bricks, sizeof (uint32_t));
    fs.read ((char *)&vc_count, sizeof (uint32_t));
    fs.read ((char *)&pc_count, sizeof (uint32_t));
    fs.read ((char *)&this->data.header, sizeof (Header));

    this->data.nodes.resize (num_nodes);
    this->data.bricks.resize (num_bricks);

    fs.read ((char *)this->data.nodes.data (), num_nodes * sizeof (uint32_t));
    fs.read ((char *)this->data.bricks.data (), num_bricks * sizeof (uint32_t));

    if (vc_count || pc_count) {
        LOG_WARN ("Data channels in scomtree are not supported by application. Ignoring them.");
    }

    fs.close ();

    const int depth = this->data.header.max_depth;

    this->state = SceneState {
        .camera = Camera (),
        .draw_method = DrawMethod::SComTreeCompute,
        .name = path.stem ().string (),
        .path = path,
        .octree_depth = depth,
        .cpu_traversed = LiteMath::min (3, depth),
        .frustum_culling_level = depth,
        .occlusion_culling_level = depth
    };

    return true;
}

SceneState& SComTreeScene::get_state () {
    return this->state;
}

void SComTreeScene::set_state (const SceneState& scene_state) {
    this->state = scene_state;
}

const SComTree& SComTreeScene::get_octree_data () const {
    return this->data;
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
    Header,
    brick_size, v_size, bits_per_value, values_per_uint, value_mask,
    bitmask_len, dimension, child_rot_shift, child_rot_mask,
    child_add_shift, child_add_mask, child_offset_mask, child_offset_off,
    node_offset_mask, uints_per_link, unique_brick_prefix,
    unique_brick_offset_mask, children_types_shift, children_types_mask,
    base_reference_shift, children_active_bits_shift, children_active_bits_mask,
    references_offset, reference_bits, reference_mask,
    references_per_uint, links_offset, max_surface_count,
    max_surface_count_per_leaf, bricks_step, bricks_arr_offset,
    nodes_arr_offset, root_node_off, has_channels, has_surfaces,
    has_multi_nodes, tex_id_off, mat_id_off, all_float_tex_id_off,
    all_int_mat_id_off, max_val, max_depth,
    user_params,
    _pad0, _pad1, _pad2, _pad3, _pad4
)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
    NodeHeadUnpacked,
    base_type,
    children_types,
    base_links_end,
    children_active
);

struct ExtStackElement {
    uint32_t links_offset;
    uint32_t transform;
    uint32_t info;
    LiteMath::uint2 p_size;
};

ExtStackElement init_root (const Header& header, const std::vector <uint32_t>& nodes) {
    uint32_t node_data = nodes [header.root_node_off];
    NodeHeadUnpacked node = unpack_node_head (header, node_data, node_data);
    ExtStackElement root;
    root.links_offset = header.root_node_off + header.links_offset + ((node.base_type) == SCOM2_CHILD_EMPTY ? 0 : 1);
    root.transform = 0;
    root.info = (0 << 24) | (node.children_types << 8) | 0;
    root.p_size = LiteMath::uint2 (0, 1);
    return root;
}

#ifdef _MSC_VER
#define bit_count(x) __popcnt(x)
#else
#define bit_count(x) __builtin_popcount(x)
#endif

struct BrickPayload {
    LiteMath::uint2 p_size;
    uint32_t child_link;
    uint32_t rotation;
};

namespace {

LiteMath::float3 interpolate_vertex (float isolevel, LiteMath::float3 p1, LiteMath::float3 p2, float valp1, float valp2) {
    if (std::abs (isolevel - valp1) < 0.00001)
        return (p1);
    if (std::abs (isolevel - valp2) < 0.00001)
        return (p2);
    if (std::abs (valp1 - valp2) < 0.00001)
        return (p1);

    float mu = (isolevel - valp1) / (valp2 - valp1);

    LiteMath::float3 p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    p.z = p1.z + mu * (p2.z - p1.z);
    return (p);
}

}

template <typename T>
concept VertexCallback = requires (T cb, std::span <Vertex, 3> tri) {
    cb (tri);
};

struct VoxelContext {
    LiteMath::float3 min_pos;
    float sdf [8];
    float size;
};

float eval_sdf_local (const VoxelContext& ctx, LiteMath::float3 p) {
    LiteMath::float3 local_p = (p - ctx.min_pos) / ctx.size;
    float tx = local_p.x, ty = local_p.y, tz = local_p.z;

    float c00 = ctx.sdf [0] * (1 - tx) + ctx.sdf [4] * tx;
    float c01 = ctx.sdf [1] * (1 - tx) + ctx.sdf [5] * tx;
    float c10 = ctx.sdf [2] * (1 - tx) + ctx.sdf [6] * tx;
    float c11 = ctx.sdf [3] * (1 - tx) + ctx.sdf [7] * tx;

    float c0 = c00 * (1 - ty) + c10 * ty;
    float c1 = c01 * (1 - ty) + c11 * ty;

    return c0 * (1 - tz) + c1 * tz;
}

void process_voxel (auto&& cb, const VoxelContext& ctx) {
    auto vertices = nlohmann::json::array ();

    int cube_index = 0;
    LiteMath::float3 corners [8];

    for (int i = 0; i < 8; ++i) {
        LiteMath::float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if ((i >> 2) & 1) corner_offset.x = ctx.size;
        if ((i >> 1) & 1) corner_offset.y = ctx.size;
        if ((i >> 0) & 1) corner_offset.z = ctx.size;
        corners [i] = ctx.min_pos + corner_offset;

        if (ctx.sdf [i] < 0.f) {
            cube_index |= (1 << i);
        }
    }

    int edge_mask = cube_index_2_edge_mask [cube_index];
    if (edge_mask == 0) {
        return;
    }

    LiteMath::float3 edge_vertices [12];
    for (int i = 0; i < 12; ++i) {
        const auto corner_indices = edge_corners [i];
        edge_vertices [i] = interpolate_vertex (0.f
                                                , corners [corner_indices.x]
                                                , corners [corner_indices.y]
                                                , ctx.sdf [corner_indices.x]
                                                , ctx.sdf [corner_indices.y]
                                                );
    }

    const int *triangle_indices = cube_index_2_triangle_indices [cube_index];
    for (int i = 0; triangle_indices [i] != -1; i += 3) {
        std::array <Vertex, 3> tri;
        for(int j = 0; j < 3; ++j) {
            auto pos = edge_vertices [triangle_indices [i + j]];
            tri [j].position = { pos.x, pos.y, pos.z, 1.f };

            const float h = ctx.size * 0.01f;

            float dx = eval_sdf_local (ctx, pos + LiteMath::float3 {h, 0, 0})
                - eval_sdf_local (ctx, pos - LiteMath::float3 {h, 0, 0});
            float dy = eval_sdf_local (ctx, pos + LiteMath::float3 {0, h, 0})
                - eval_sdf_local (ctx, pos - LiteMath::float3 {0, h, 0});
            float dz = eval_sdf_local (ctx, pos + LiteMath::float3 {0, 0, h})
                - eval_sdf_local (ctx, pos - LiteMath::float3 {0, 0, h});

            LiteMath::float3 norm = LiteMath::normalize (LiteMath::float3 {dx, dy, dz});
            tri [j].normal = { norm.x, norm.y, norm.z, 0.0f };

            // tri [j].color = LiteMath::float4 (1.f, 1.f, 1.f, 1.f);
            tri [j].color = LiteMath::to_float4 ((norm / 2.f) + 0.5f, 1.f);
        }
        cb (tri, ctx);
    }
}

void process_brick (auto&& cb, const Header& header, const std::vector <uint32_t>& nodes, const std::vector <uint32_t>& bricks, BrickPayload payload) {
    const LiteMath::uint3 p = LiteMath::uint3 (payload.p_size.x >> 16, payload.p_size.x & 0xFFFF, payload.p_size.y >> 16);
    const uint32_t level_sz = payload.p_size.y & 0xFFFF;

    const float d = 2.0f / float (level_sz);
    const LiteMath::float3 p_f = LiteMath::float3 (p);
    const float max_val = get_max_sdf_val (float (level_sz));

    uint32_t link_data = nodes [payload.child_link];
    SdfDAGDataEdge de = unpack_data_edge (header, max_val, link_data, link_data);
    uint32_t offset = header.bricks_step * de.data_offset;
    uint32_t rotation_index = rotation_add [payload.rotation * 48 + de.rotation_id];
    float add = de.add;

    for (uint32_t vox_idx = 0; vox_idx < 8; ++vox_idx) {
        float vmin = std::numeric_limits <float>::min ();
        float vmax = std::numeric_limits <float>::max ();
        float values [8] = {};

        LiteMath::int3 vox_p = LiteMath::int3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));

        for (int i = 0; i < 8; ++i) {
            LiteMath::int4 value_p = LiteMath::to_int4 (vox_p, 0) + LiteMath::int4 (((i & 4) >> 2), ((i & 2) >> 1), (i & 1), 1);
            uint32_t vId0 = static_cast <uint32_t> (dot (rotation_modifiers [rotation_index], value_p));
            const uint32_t p_val = bricks [offset + vId0 / header.values_per_uint];
            const uint32_t p_off = (vId0 % header.values_per_uint) * header.bits_per_value;
            float val = max_val * (2.0f * ((p_val >> p_off) & header.value_mask) / float(header.value_mask) - 1) + add;
            values [i] = val - 0.5f * SCOM2_EPS * d;
            vmin = std::min (vmin, val);
            vmax = std::max (vmax, val);
        }

        if (vmin <= 0.0f && vmax >= 0.0f) {
            VoxelContext ctx;
            ctx.min_pos = LiteMath::float3 (-1, -1, -1) + d * p_f + 0.5f * d * LiteMath::float3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));
            ctx.size = 0.5f * d;
            std::copy (std::begin (values), std::end (values), std::begin (ctx.sdf));

            process_voxel (cb, ctx);
        }
    }
}

void traverse_scomtree_core (const Header& header, const std::vector <uint32_t>& nodes, const std::vector <uint32_t>& bricks, auto&& cb) {
    nlohmann::json result;
    nlohmann::json active_leafs = nlohmann::json::array ();

    ExtStackElement stack [16];

    int top = 0;
    float d = 1.0f;

    ExtStackElement cur = init_root (header, nodes);

    stack [0].links_offset = 0xFFFFFFFFu;
    stack [0].info = 0;
    stack [0].transform = 0;
    stack [0].p_size = LiteMath::uint2 (0, 0);

    assert (header.children_types_shift == 8);
    assert (header.children_types_mask == 0xFFFF);
    assert (header.children_active_bits_shift == 24);

    while (top >= 0) {
        const uint32_t child = cur.info & 0x7;
        assert (child < 8);

        const LiteMath::uint3 voxel_pos = LiteMath::uint3 ((child & 4) >> 2, (child & 2) >> 1, child & 1); // (0,0,0)..(1,1,1)
        const uint32_t child_n = static_cast <uint32_t> (dot (rotation_modifiers [2 * 48 + cur.transform]
            , LiteMath::int4 (static_cast <int> (voxel_pos.x), static_cast <int> (voxel_pos.y), static_cast <int> (voxel_pos.z), 1)));
        const uint32_t child_n_mask = 1u << (2 * child_n);

        const uint32_t children_types_mask = (cur.info >> header.children_types_shift) & header.children_types_mask;
        const uint32_t active_children_mask = ((children_types_mask & 0x0000AAAAu) >> 1) | (children_types_mask & 0x00005555u);
        const uint32_t child_link = cur.links_offset + bit_count (active_children_mask & (child_n_mask - 1));
        const uint32_t child_has_data = active_children_mask & child_n_mask;
        const uint32_t child_is_leaf = cur.info & (1u << (header.children_types_shift + 2 * child_n));
        const uint32_t rot_id = cur.info >> header.children_active_bits_shift;

        d = 1.0f / float (cur.p_size.y & 0xFFFF);

        std::string indent (top * 4, ' ');

        if (child_has_data == 0) {
            LOG_TRACE ("{}Child {}: No data", indent, child_n);
        } else if (child_is_leaf > 0) {
            LiteMath::float3 pf2 = LiteMath::float3 (
                float (cur.p_size.x >> 16)    + (((child_n & 4) > 0) ? 1.0f : 0.5f)
                , float (cur.p_size.x & 0xFFFF) + (((child_n & 2) > 0) ? 1.0f : 0.5f)
                , float (cur.p_size.y >> 16)    + (((child_n & 1) > 0) ? 1.0f : 0.5f));
            LOG_TRACE ("{}Leaf {}: pos={}", indent, child_n, pf2 * d);
        } else {
            LOG_TRACE ("{}Node {}: Entering... (d={})", indent, child_n, d);
        }

        if (child_has_data == 0) {
            const uint32_t next_child = child + 1;
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else if (child_is_leaf > 0) {
            BrickPayload payload;
            payload.p_size = (cur.p_size << 1) | LiteMath::uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
            payload.child_link = child_link;
            payload.rotation = rot_id;
            process_brick (cb, header, nodes, bricks, payload);

            const uint32_t next_child = child + 1;
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else {
            const uint32_t next_child = child + 1;
            if (next_child < 8) {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
                stack [++top] = cur; // NOTE: return parent we previously popped for other children
            }

            d = 0.5f / float (cur.p_size.y & 0xFFFF);

            uint32_t link_data = nodes [child_link];
            SdfDAGChildEdge ce = unpack_child_edge (header, link_data, link_data);

            uint32_t node_data = nodes [ce.child_offset];
            NodeHeadUnpacked node = unpack_node_head (header, node_data, node_data);

            uint32_t rot_idx = rotation_add [rot_id * 48 + ce.rotation_id];
            cur.links_offset = ce.child_offset + header.links_offset + (node.base_type == SCOM2_CHILD_EMPTY ? 0 : 1);
            cur.transform = rot_idx;
            cur.info = (rot_idx << 24) | (node.children_types << 8);
            cur.p_size = (cur.p_size << 1) | LiteMath::uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
        }
    }
}

nlohmann::json dump_active_leafs (const Header& h, const auto& n, const auto& b) {
    nlohmann::json active_leafs = nlohmann::json::array ();

    auto json_cb = [&] (auto tri, const VoxelContext& ctx) {
        nlohmann::json leaf_data;
        leaf_data ["min_pos"] = { ctx.min_pos.x, ctx.min_pos.y, ctx.min_pos.z };
        leaf_data ["sdf"] = { ctx.sdf [0], ctx.sdf [1], ctx.sdf [2], ctx.sdf [3], ctx.sdf [4], ctx.sdf [5], ctx.sdf [6], ctx.sdf [7] };
        for (const auto& v : tri) {
            leaf_data ["vertices"].push_back ({
                {"position", {v.position.x, v.position.y, v.position.z, 1.0f}}
                , {"normal", {0, 0, 0, 1}}
                , {"color", {v.color.x, v.color.y, v.color.z, 1.0f}}
            });
        }
        active_leafs.push_back (leaf_data);
    };

    traverse_scomtree_core (h, n, b, json_cb);
    return active_leafs;
}

void to_json (nlohmann::json& j, const SComTree& tree) {
    j = nlohmann::json {
        {"name", tree.name}
        , {"header", tree.header}
        , {"active_leafs", dump_active_leafs (tree.header, tree.nodes, tree.bricks)}
    };
}

void SComTreeScene::dump_as_json (const std::filesystem::path& path) const {
    try {
        nlohmann::json j = this->data;

        std::ofstream file (path);
        if (!file.is_open ()) {
            return;
        }

        file << j.dump (4);

        file.close ();
        LOG_INFO ("[SComTreeScene] Dump successful.");

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR ("[SComTreeScene] JSON dump error: {}", e.what ());
    } catch (const std::exception& e) {
        LOG_ERROR ("[SComTreeScene] An unexpected error occurred during dump: {}", e.what ());
    }
}

Mesh SComTreeScene::generate_mesh () const {
    Mesh mesh;
    auto mesh_cb = [&](auto tri, const VoxelContext& /*ctx*/) {
        mesh.add_triangle (tri [0], tri [1], tri [2]);
    };

    traverse_scomtree_core (this->data.header, this->data.nodes, this->data.bricks, mesh_cb);

    return mesh;
}

namespace {

void add_voxel_to_mesh (Mesh& mesh, LiteMath::float3 min_p, float size) {
    float s = size * 0.9f;
    LiteMath::float3 p [8];
    for (int i = 0; i < 8; ++i) {
        p [i] = min_p + LiteMath::float3 (
            (i & 4) ? s : 0.0f
            , (i & 2) ? s : 0.0f
            , (i & 1) ? s : 0.0f
        );
    }

    auto color = LiteMath::to_float4 (LiteMath::abs (LiteMath::normalize (min_p)), 1.0f);
    auto n = LiteMath::float4 (0, 0, 0, 0);

    auto add_f = [&] (int i0, int i1, int i2, int i3) {
        mesh.add_triangle ({LiteMath::to_float4 (p [i0],1), n, color}, {LiteMath::to_float4 (p [i1],1), n, color}, {LiteMath::to_float4 (p [i2],1), n, color});
        mesh.add_triangle ({LiteMath::to_float4 (p [i0],1), n, color}, {LiteMath::to_float4 (p [i2],1), n, color}, {LiteMath::to_float4 (p [i3],1), n, color});
    };

    add_f (0, 4, 6, 2); // Left
    add_f (1, 3, 7, 5); // Right
    add_f (0, 1, 5, 4); // Bottom
    add_f (2, 6, 7, 3); // Top
    add_f (0, 2, 3, 1); // Back
    add_f (4, 5, 7, 6); // Front
}

}

Mesh SComTreeScene::generate_voxel_mesh () const {
    Mesh mesh;

    auto voxel_cb = [&] (auto /*tri*/, const VoxelContext& ctx) {
        static LiteMath::float3 last_pos = {-100, -100, -100};
        if (LiteMath::length (ctx.min_pos - last_pos) > 1e-5f) {
            add_voxel_to_mesh (mesh, ctx.min_pos, ctx.size * 0.5f);
            last_pos = ctx.min_pos;
        }
    };

    traverse_scomtree_core (this->data.header, this->data.nodes, this->data.bricks, voxel_cb);
    return mesh;
}

SComTreeScene::~SComTreeScene () {
    this->data.nodes.clear ();
    this->data.bricks.clear ();
}

std::vector <SComTreeStackElement> get_octree_subtrees_payloads (const SComTree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO:
    return {};
}

void SComTreeScene::invalidate_cache () {
    this->cached_all_subtrees = get_octree_subtrees_payloads (this->data, this->state.cpu_traversed);
}

std::vector <SComTreeStackElement> SComTreeScene::collect_visible_subtrees (const FrustumGeometry& frustum) const {
    std::vector <SComTreeStackElement> visible;
    frustum_culling (this->cached_all_subtrees, frustum, visible);
    return visible;
}

} // sdf_raster

