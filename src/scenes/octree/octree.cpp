// scenes/octree/octree.cpp
#include "scenes/octree/octree.hpp"

#include "engine/utils/frustum_culling.hpp"
#include "logger.hpp"
#include "shader_common.hpp"

#include <fstream>
#include <stack>

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
        .name = path.stem ().string (),
        .path = path,
        .octree_depth = depth,
        .cpu_traversed = LiteMath::min (3, depth),
        .frustum_culling_level = depth,
        .occlusion_culling_level = depth
    };
    this->set_current_draw_method (DrawMethod::OctreeCompute);

    this->invalidate_cache ();

    return true;
}

SceneState& SdfOctreeScene::get_state () {
    return this->state;
}

void SdfOctreeScene::set_state (const SceneState& scene_state) {
    this->apply_state (scene_state);
}

const SdfOctree& SdfOctreeScene::get_octree_data () const {
    return this->data;
}

SdfOctreeScene::~SdfOctreeScene () {
    this->data.nodes.clear ();
}

namespace {

struct StackFrame {
    uint32_t node_idx;
    LiteMath::float3 min_corner;
    float voxel_size;
    int level;
};

int calc_cube_index (const float arr [8]) {
    int cube_index = 0;

    for (int i = 0; i < 8; ++i) {
        if (arr [i] < 0.0f) {
            cube_index |= (1 << i);
        }
    }

    return cube_index;
};

std::vector <NodeContext> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend) {
    std::vector <NodeContext> payloads;

    if (scene.nodes.empty ()) {
        return payloads;
    }

    std::stack <StackFrame> s;

    LiteMath::float3 root_min_corner = {-1.0f, -1.0f, -1.0f};
    float root_voxel_size = 2.0f;
    uint32_t root_node_idx = 0;

    s.push ({root_node_idx, root_min_corner, root_voxel_size, 0});

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        const SdfOctreeNode& node = scene.nodes [current.node_idx];

        if (current.level >= max_level_to_descend || node.offset == 0) {
            int cube_index = calc_cube_index (node.values);
            if (node.offset == 0 && (cube_index == 0 || cube_index == 255)) {
                continue; // no triangles
            }
            payloads.push_back ({
                current.min_corner.x,
                current.min_corner.y,
                current.min_corner.z,
                current.voxel_size,
                static_cast <int> (current.node_idx),
                cube_index
            });
            continue;
        }

        float half = current.voxel_size * 0.5f;
        for (int i = 7; i >= 0; --i) {
            LiteMath::float3 child_min_corner = current.min_corner;

            if ((i & 1) != 0) child_min_corner.x += half;
            if ((i & 2) != 0) child_min_corner.y += half;
            if ((i & 4) != 0) child_min_corner.z += half;

            uint32_t child_node_idx = node.offset + i;

            s.push ({
                child_node_idx,
                child_min_corner,
                half,
                current.level + 1
            });
        }
    }

    return payloads;
}

}

void SdfOctreeScene::invalidate_cache () {
    this->cached_all_subtrees = get_octree_subtrees_payloads (this->data, this->state.cpu_traversed);
}

std::vector <NodeContext> SdfOctreeScene::collect_visible_subtrees (const FrustumGeometry& frustum) const {
    std::vector <NodeContext> visible;
    frustum_culling (this->cached_all_subtrees, frustum, visible);
    return visible;
}

std::span <const DrawMethod> SdfOctreeScene::get_available_draw_methods () const {
    return this->available_methods;
}

} // sdf_raster

