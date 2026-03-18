#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"

#include <filesystem>

struct SdfOctreeNode;

namespace sdf_raster {

class OctreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState get_state () const override;

    ~OctreeScene () override;

private:
    SceneState m_state;
    std::vector <SdfOctreeNode> m_nodes;
};

} // sdf_raster

