#pragma once

#include "scenes/scene.hpp"

namespace sdf_raster {

class OctreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState get_state () const override;

    ~OctreeScene () override;
};

} // sdf_raster

