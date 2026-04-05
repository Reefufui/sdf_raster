#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"

#include <filesystem>

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

    ~SdfOctreeScene () override;

    const SdfOctree& get_octree_data () const;

private:
    SceneState state;
    SdfOctree data;
};

} // sdf_raster

