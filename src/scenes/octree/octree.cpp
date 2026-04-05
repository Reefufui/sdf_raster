#include "scenes/octree/octree.hpp"
#include "shader_common.hpp"

#include <fstream>

namespace {

int get_octree_max_depth (const std::vector <SdfOctreeNode>& nodes) {
    if (nodes.empty ()) {
        return 0;
    }

    int max_overall_depth = 0;

    struct StackFrame {
        uint32_t node_idx;
        int level;
    };
    std::stack <StackFrame> s;

    uint32_t root_node_idx = 0;
    s.push ({root_node_idx, 0});

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        max_overall_depth = std::max (max_overall_depth, current.level);

        const SdfOctreeNode& node = nodes [current.node_idx];

        if (node.offset == 0) {
            continue;
        }

        for (int i = 0; i < 8; ++i) {
            uint32_t child_node_idx = node.offset + i;
            s.push ({ child_node_idx, current.level + 1 });
        }
    }

    return max_overall_depth;
}

} // anon

namespace sdf_raster {

bool SdfOctreeScene::load (const std::filesystem::path& path) {
    std::ifstream fs (path, std::ios::binary);
    unsigned sz = 0;
    fs.read ((char *) &sz, sizeof (unsigned));
    this->data.nodes.resize (sz);
    fs.read ((char *) this->data.nodes.data (), this->data.nodes.size () * sizeof (SdfOctreeNode));
    fs.close ();

    const int depth = get_octree_max_depth (this->data.nodes);

    this->state = SceneState {
        .camera = Camera (),
        .draw_method = DrawMethod::ImplicitCompute,
        .name = path.stem ().string (),
        .path = path,
        .octree_depth = depth,
        .cpu_traversed = LiteMath::min (3, depth),
        .frustum_culling_level = depth,
        .occlusion_culling_level = depth
    };

    return true;
}

SceneState& SdfOctreeScene::get_state () {
    return this->state;
}

void SdfOctreeScene::set_state (const SceneState& scene_state) {
    this->state = scene_state;
}

const SdfOctree& SdfOctreeScene::get_octree_data () const {
    return this->data;
}

SdfOctreeScene::~SdfOctreeScene () {
    this->data.nodes.clear ();
}

} // sdf_raster

