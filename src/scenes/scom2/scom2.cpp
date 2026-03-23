#include "scenes/scom2/scom2.hpp"

#include "logger.hpp"
#include "scenes/scom2/defs.hpp"
#include "scenes/scom2/utils.hpp"

#include "shader_common.hpp"

#include <fstream>

namespace {

int get_scom2_max_depth (const sdf_raster::SCom2Tree& /*scom2*/) {
    // TODO
    return 0;
}

} // anon

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

    const int depth = get_scom2_max_depth (this->data);

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
};

ExtStackElement init_root (const Header& header, const std::vector <uint32_t>& nodes) {
    LOG_INFO ("root: offset={}, overall size={}", header.root_node_off, nodes.size ());
    uint32_t node_data = nodes [header.root_node_off];
    NodeHeadUnpacked node = unpack_node_head (header, node_data, node_data);
    LOG_INFO ("root base type: {}", (node.base_type == SCOM2_CHILD_EMPTY) ? "SCOM2_CHILD_EMPTY"
         : (node.base_type == SCOM2_CHILD_LEAF_VOLUME) ? "SCOM2_CHILD_LEAF_VOLUME"
         : (node.base_type == SCOM2_CHILD_LEAF_SURFACE) ? "SCOM2_CHILD_LEAF_SURFACE"
         : (node.base_type == SCOM2_CHILD_NODE) ? "SCOM2_CHILD_NODE" : "UNKNOWN");
    LOG_INFO ("root base_links_end={}", node.base_links_end);
    LOG_INFO ("root children_types={:016b}", node.children_types);
    LOG_INFO ("root children active\n76543210\n{:8b}", node.children_active);
    return ExtStackElement {
        .links_offset = header.root_node_off + header.links_offset + ((node.base_type) == SCOM2_CHILD_EMPTY ? 0 : 1),
        .transform = 0,
        .info = (0 << 24) | (node.children_types << 8) | 0
        /*      rotation  | children types             | current child index */
    };
}

#ifdef _MSC_VER
#define bit_count(x) __popcnt(x)
#else
#define bit_count(x) __builtin_popcount(x)
#endif

nlohmann::json dump_as_json (const Header& header, const std::vector <uint32_t>& nodes, const std::vector <uint32_t>& bricks) {
    ExtStackElement stack [16];

    ExtStackElement cur = init_root (header, nodes);

    int top = 0;

    stack [top].links_offset = 0xFFFFFFFFu;
    stack [top].info = 0;
    stack [top].transform = 0;

    assert (header.children_types_shift == 8);
    assert (header.children_active_bits_shift == 24);

    LOG_INFO ("root info\nrrrrrrrr7766554433221100iiiiiiii\n{:032b}", cur.info);

    while (top >= 0) {
        const uint32_t child = cur.info & 0x7;
        assert (child >= 0 && child < 8);
        LOG_INFO ("child\n------------------------iiiiiiii\n{:032b}", child);

        const LiteMath::uint3 child_offset = LiteMath::uint3 ((child & 4) >> 2, (child & 2) >> 1, child & 1); // (0,0,0)..(1,1,1)
        assert (cur.transform == 0); // TODO: transform
        const uint32_t child_n = child; // TODO: transform

        const uint32_t children_types_mask = (cur.info >> header.children_types_shift) & header.children_types_mask; // 16bit
        LOG_INFO ("children_types_mask\n----------------7766554433221100\n{:032b}", children_types_mask);

        const uint32_t active_children_mask = ((children_types_mask & 0x0000AAAAu) >> 1) | (children_types_mask & 0x00005555u); // 16bit (odd)
        LOG_INFO ("active_children_mask\n----------------7766554433221100\n{:032b}", active_children_mask);

        const uint32_t child_n_mask = 1u << (2 * child_n); // 16bit (odd)
        LOG_INFO ("child_n_mask\n----------------7766554433221100\n{:032b}", child_n_mask);

        const uint32_t left_from_child_n_mask = child_n_mask - 1; // 16bit
        LOG_INFO ("left_from_child_n_mask\n----------------7766554433221100\n{:032b}", left_from_child_n_mask);

        const uint32_t child_link_offset = bit_count (active_children_mask & left_from_child_n_mask);
        const uint32_t child_link = cur.links_offset + child_link_offset;
        LOG_INFO ("child_link={}+{}={}", cur.links_offset, child_link_offset, child_link);

        const uint32_t child_has_data = active_children_mask & child_n_mask;
        const uint32_t child_is_leaf = (cur.info >> (header.children_types_shift + 1)) & child_n_mask;
        const uint32_t rot_id = cur.info >> header.children_active_bits_shift;

        assert (rot_id == 0); // TODO: rotations

        LOG_INFO ("child_n={}, active_children_mask={}, child_link={}, child_has_data={}, child_is_leaf={}"
            , child_n, active_children_mask, child_link
            , child_has_data > 0, child_is_leaf > 0);

        if (child_has_data == 0) {
            LOG_WARN ("Child {}: no data.", child);
            const uint32_t next_child = child + 1; // TODO: rotations?
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else if (child_is_leaf > 0) {
            LOG_WARN ("Child {}: is leaf.", child);

            // uint32_t level_sz = 2 * (cur.p_size.y & 0xFFFF);
            // const float max_sdf = get_max_sdf_val (float level_size);
            /*

            uint32_t link_data = nodes [child_link];
            SdfDAGDataEdge de = unpack_data_edge (header, max_val, link_data, link_data);
            uint32_t offset = header.bricks_step * de.data_offset;
            uint32_t rotation_index = 0; // TODO:
            float add = de.add;

            uint32_t current_voxel = 0;
            while (current_voxel < 8) {
                float values [8];
                for (int i = 0; i < 8; i++) {
                    LiteMath::int4 pI = LiteMath::int4 ((current_voxel & 4) >> 2, (current_voxel & 2) >> 1, current_voxel & 1, 0)
                        + LiteMath::int4 ((i & 4) >> 2 , (i & 2) >> 1, i & 1, 1);

                    // uint32_t vId0 = 0;

                    // uint32_t p_val = bricks [offset + vId0 / header.values_per_uint];
                    // float val = max_val * (2.0f * ((p_val >> p_off) & header.value_mask) / float(header.value_mask) - 1) + add;
                    // values [i] = val - 0.5f * m_preset.compact_sdf_eps * d;
                }
            }
            */

            const uint32_t next_child = child + 1; // TODO: rotations?
            if (next_child >= 8) {
                cur = stack [top--];
            } else {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
            }
        } else {
            LOG_WARN ("Child {}: is node.", child);
            const uint32_t next_child = child + 1; // TODO: rotations?
            if (next_child < 8) {
                cur.info = next_child | (cur.info & 0xFFFFFF00u);
                stack [++top] = cur; // NOTE: return parent we previously popped for other children
            }
            // TODO: voxel_size?

            uint32_t link_data = nodes [child_link];
            SdfDAGChildEdge ce = unpack_child_edge (header, link_data, link_data);

            LOG_INFO ("ce: child_offset={}, rotation_id={}.", ce.child_offset, ce.rotation_id);

            uint32_t node_data = nodes [ce.child_offset];
            NodeHeadUnpacked node = unpack_node_head (header, node_data, node_data);

            assert (ce.rotation_id == 48); // TODO: rotations: modify cur.transform. 48 means that leaf is unique
            const uint32_t next_node = 0;

            cur.links_offset = ce.child_offset + header.links_offset + (node.base_type == SCOM2_CHILD_EMPTY ? 0 : 1);
            cur.info = (0 /* rot_idx */ << 24) | (node.children_types << 8) | next_node;
        }
    }

    ////

    nlohmann::json nodes_json = nlohmann::json::array ();

    for (size_t i = 0; i < 10; ++i) {
        nodes_json.push_back ({
            {"index", i}
            , {"raw_value", nodes[i]}
            , {"head", unpack_node_head (header, nodes[i], nodes[i])}
        });
    }

    return nodes_json;
}

inline void to_json (nlohmann::json& j, const SCom2Tree& tree) {
    j = nlohmann::json {
        {"name", tree.name}
        , {"header", tree.header}
        , {"tree", dump_as_json (tree.header, tree.nodes, tree.bricks)}
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

