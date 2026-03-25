#include "scenes/scom2/scom2.hpp"

#include "logger.hpp"
#include "scenes/scom2/defs.hpp"
#include "scenes/scom2/utils.hpp"

#include "shader_common.hpp"

#include <fstream>

namespace sdf_raster {

bool SCom2TreeScene::load (const std::filesystem::path& path) {
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
        LOG_ERROR ("Legacy scom2 is not supported.");
        return false;
    }

    fs.read ((char *)&version, sizeof (uint32_t));

    if (version != SCOM2_VERSION) {
        fs.close ();
        LOG_ERROR ("SCom2 version mismatch (save is version {}, current version is {})", version, SCOM2_VERSION);
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
        LOG_WARN ("Data channels in scom2 are not supported by application. Ignoring them.");
    }

    fs.close ();

    const int depth = this->data.header.max_depth;

    this->state = SceneState {
        .camera = Camera (),
        .name = path.stem ().string (),
        .path = path,
        .octree_depth = depth,
        .cpu_traversed = LiteMath::min (3, depth),
        .frustum_culling_level = depth,
        .occlusion_culling_level = depth
    };

    return true;
}

SceneState SCom2TreeScene::get_state () const {
    return this->state;
}

const SCom2Tree& SCom2TreeScene::get_octree_data () const {
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

template <typename T>
void process_leaf (T& out, const Header& header, const std::vector <uint32_t>& nodes, const std::vector <uint32_t>& bricks, BrickPayload payload) {
    LiteMath::uint3 p = LiteMath::uint3 (payload.p_size.x >> 16, payload.p_size.x & 0xFFFF, payload.p_size.y >> 16);
    uint32_t level_sz = payload.p_size.y & 0xFFFF;

    const float d = 2.0f / float (level_sz);
    LiteMath::float3 p_f = LiteMath::float3 (p);
    float max_val = get_max_sdf_val (float (level_sz));

    uint32_t link_data = nodes [payload.child_link];
    SdfDAGDataEdge de = unpack_data_edge (header, max_val, link_data, link_data);
    uint32_t offset = header.bricks_step * de.data_offset;
    assert (payload.rotation == 0); // TODO: rotations
    uint32_t rotation_index = 0; // TODO:
    float add = de.add;

    for (uint32_t vox_idx = 0; vox_idx < 8; ++vox_idx) {
        float vmin = std::numeric_limits <float>::min ();
        float vmax = std::numeric_limits <float>::max ();
        float values [8] = {};

        LiteMath::uint3 vox_p = LiteMath::uint3 (((vox_idx & 4) >> 2), ((vox_idx & 2) >> 1), (vox_idx & 1));
        LiteMath::float3 vox_p_f = LiteMath::float3 (vox_p);

        for (int i = 0; i < 8; ++i) {
            LiteMath::uint4 value_p = LiteMath::to_uint4 (vox_p, 0) + LiteMath::uint4 (((i & 4) >> 2), ((i & 2) >> 1), (i & 1), 1);
            LiteMath::uint4 rot_0_modifier = LiteMath::uint4 (header.v_size * header.v_size, header.v_size, 1, 0);
            uint32_t vId0 = uint32_t (dot (rot_0_modifier, value_p));

            const uint32_t p_val = bricks [offset + vId0 / header.values_per_uint];
            const uint32_t p_off = (vId0 % header.values_per_uint) * header.bits_per_value;
            float val = max_val * (2.0f * ((p_val >> p_off) & header.value_mask) / float(header.value_mask) - 1) + add;
            values [i] = val - 0.5f * SCOM2_EPS * d;
            vmin = std::min (vmin, val);
            vmax = std::max (vmax, val);
        }

        if (vmin <= 0.0f && vmax >= 0.0f) {
            nlohmann::json leaf_data;
            LiteMath::float3 min_pos = LiteMath::float3 (-1, -1, -1) + d * p_f + 0.5f * d * vox_p_f;
            leaf_data ["min_pos"] = { min_pos.x, min_pos.y, min_pos.z };
            leaf_data ["sdf"] = { values [0], values [1], values [2], values [3], values [4], values [5], values [6], values [7] };
            out.push_back (leaf_data);
        }
    }
}

nlohmann::json dump_active_leafs (const Header& header, const std::vector <uint32_t>& nodes, const std::vector <uint32_t>& bricks) {
    nlohmann::json result;
    nlohmann::json active_leafs = nlohmann::json::array ();

    float final_voxel_size = 0.0f;

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
        assert (child >= 0 && child < 8);

        const LiteMath::uint3 voxel_pos = LiteMath::uint3 ((child & 4) >> 2, (child & 2) >> 1, child & 1); // (0,0,0)..(1,1,1)
        assert (cur.transform == 0); // TODO: transform
        const uint32_t child_n = child; // TODO: transform
        const uint32_t child_n_mask = 1u << (2 * child_n);

        const uint32_t children_types_mask = (cur.info >> header.children_types_shift) & header.children_types_mask;
        const uint32_t active_children_mask = ((children_types_mask & 0x0000AAAAu) >> 1) | (children_types_mask & 0x00005555u);
        const uint32_t child_link = cur.links_offset + bit_count (active_children_mask & (child_n_mask - 1));
        const uint32_t child_has_data = active_children_mask & child_n_mask;
        const uint32_t child_is_leaf = cur.info & (1u << (header.children_types_shift + 2 * child_n));
        const uint32_t rot_id = cur.info >> header.children_active_bits_shift;

        assert (rot_id == 0); // TODO: rotations

        d = 1.0f / float (cur.p_size.y & 0xFFFF);

        std::string indent (top * 4, ' ');

        if (child_has_data == 0) {
            LOG_TRACE ("{}Child {}: No data", indent, child);
        } else if (child_is_leaf > 0) {
            LiteMath::float3 pf2 = LiteMath::float3 (
                float (cur.p_size.x >> 16)    + (((child & 4) > 0) ? 1.0f : 0.5f)
                , float (cur.p_size.x & 0xFFFF) + (((child & 2) > 0) ? 1.0f : 0.5f)
                , float (cur.p_size.y >> 16)    + (((child & 1) > 0) ? 1.0f : 0.5f));
            LOG_TRACE ("{}Leaf {}: pos={}", indent, child, pf2 * d);
        } else {
            LOG_TRACE ("{}Node {}: Entering... (d={})", indent, child, d);
        }

        if (child_has_data == 0) {
            const uint32_t next_child = child + 1; // TODO: rotations?
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else if (child_is_leaf > 0) {
            uint32_t level_sz = 2 * (cur.p_size.y & 0xFFFF);
            d = 2.0f / float (level_sz);
            final_voxel_size = d * 0.5f;

            BrickPayload payload;
            payload.p_size = (cur.p_size << 1) | LiteMath::uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
            payload.child_link = child_link;
            payload.rotation = 0; // TODO: rotations
            process_leaf (active_leafs, header, nodes, bricks, payload);

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

            // uint32_t rotIdx = m_RotAddTable[rotId * ROT_COUNT + ce.rotation_id];
            cur.links_offset = ce.child_offset + header.links_offset + (node.base_type == SCOM2_CHILD_EMPTY ? 0 : 1);
            // cur.transform = m_RotAddTable[cur.transform * ROT_COUNT + ce.rotation_id];
            cur.info = (0 /* rot_idx */ << 24) | (node.children_types << 8);
            cur.p_size = (cur.p_size << 1) | LiteMath::uint2 (((child & 4) << (16 - 2)) | ((child & 2) >> 1), (child & 1) << 16);
        }
    }

    result ["voxels"] = active_leafs;
    result ["size"] = final_voxel_size;

    return result;
}

inline void to_json (nlohmann::json& j, const SCom2Tree& tree) {
    j = nlohmann::json {
        {"name", tree.name}
        , {"header", tree.header}
        , {"active_leafs", dump_active_leafs (tree.header, tree.nodes, tree.bricks)}
    };
}

void SCom2TreeScene::dump_as_json (const std::filesystem::path& path) const {
    try {
        nlohmann::json j = this->data;

        std::ofstream file (path);
        if (!file.is_open ()) {
            return;
        }

        file << j.dump (4);

        file.close ();
        LOG_INFO ("[SCom2TreeScene] Dump successful.");

    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR ("[SCom2TreeScene] JSON dump error: {}", e.what ());
    } catch (const std::exception& e) {
        LOG_ERROR ("[SCom2TreeScene] An unexpected error occurred during dump: {}", e.what ());
    }
}

SCom2TreeScene::~SCom2TreeScene () {
    this->data.nodes.clear ();
    this->data.bricks.clear ();
}

} // sdf_raster

