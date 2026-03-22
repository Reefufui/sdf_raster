#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"
#include "scenes/scom2/header.hpp"

#include <cstdint>
#include <vector>

namespace sdf_raster {

struct SCom2Tree {
    std::string name;

    scom2::Header header;
    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;
};

class SCom2TreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState get_state () const override;

    ~SCom2TreeScene () override;

    const SCom2Tree& get_octree_data () const;

private:
    SceneState state;
    SCom2Tree data;
};

} // sdf_raster

