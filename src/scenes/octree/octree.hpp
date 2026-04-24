#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"

#include <filesystem>

struct FrustumGeometry;
struct NodeContext;
struct SdfOctreeNode;

namespace sdf_raster {

struct SdfOctree {
    std::string name; // TODO: remove
    std::vector <SdfOctreeNode> nodes;
};

class SdfOctreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState& get_state () override;

    void set_state (const SceneState& scene_state) override;

    std::span <const DrawMethod> get_available_draw_methods () const override;

    ~SdfOctreeScene () override;

    const SdfOctree& get_octree_data () const;

    std::vector <NodeContext> collect_visible_subtrees (const FrustumGeometry& frustum) const;

protected:
    void invalidate_cache () override;

private:
    SdfOctree data;

    std::vector <NodeContext> cached_all_subtrees;
    static inline constexpr std::array <DrawMethod, 2> available_methods = {{
        DrawMethod::OctreeCompute, DrawMethod::OctreeMesh
    }};
};

} // sdf_raster

