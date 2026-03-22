#include "logger.hpp"
#include "scenes/scom2/defs.hpp"
#include "scenes/scom2/scom2.hpp"

#include "shader_common.hpp"

#include <fstream>

namespace {

int get_scom2_max_depth (const sdf_raster::SCom2Tree& /*scom2*/) {
    // TODO
    return 0;
}

} // anon

namespace sdf_raster {

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
        printf ("[ERROR] SCom2 version mismatch (save is version %u, current version is %u)\n", version, SCOM2_VERSION);
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

inline void to_json (nlohmann::json& j, const SCom2Tree& tree) {
    j = nlohmann::json {
        {"name", tree.name}
        , {"header", tree.header}
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

