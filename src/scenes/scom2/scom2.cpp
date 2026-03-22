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

bool SCom2TreeScene::load (const std::filesystem::path& path) {
    std::ifstream fs (path, std::ios::binary);

    uint32_t magic_number = 0;
    uint32_t version = 0;
    uint32_t num_nodes = 0;
    uint32_t num_bricks = 0;
    uint32_t vc_count = 0;
    uint32_t pc_count = 0;

    fs.read ((char *)&magic_number, sizeof (uint32_t));

    if (magic_number != scom2::SCOM2_MAGIC_NUMBER) {
        fs.close ();
        LOG_ERROR ("Legacy scom2 is not supported.");
        return false;
    }

    fs.read ((char *)&version, sizeof (uint32_t));

    if (version != scom2::SCOM2_VERSION) {
        fs.close ();
        printf ("[ERROR] SCom2 version mismatch (save is version %u, current version is %u)\n", version, scom2::SCOM2_VERSION);
        return false;
    }

    fs.read ((char *)&num_nodes, sizeof (uint32_t));
    fs.read ((char *)&num_bricks, sizeof (uint32_t));
    fs.read ((char *)&vc_count, sizeof (uint32_t));
    fs.read ((char *)&pc_count, sizeof (uint32_t));
    fs.read ((char *)&this->data.header, sizeof (scom2::Header));

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

SCom2TreeScene::~SCom2TreeScene () {
    this->data.nodes.clear ();
    this->data.bricks.clear ();
}

} // sdf_raster

