#pragma once

#include "mesh.hpp"
#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"
#include "scenes/scom2/header.hpp"

#include <cstdint>
#include <vector>

namespace sdf_raster {

struct SCom2Tree {
    std::string name;

    Header header;
    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;
};

class SCom2TreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState get_state () const override;

    ~SCom2TreeScene () override;

    const SCom2Tree& get_octree_data () const;

    void dump_as_json (const std::filesystem::path& path) const;

    Mesh generate_mesh () const;

    Mesh generate_voxel_mesh () const;

private:
    SceneState state;
    SCom2Tree data;
};

} // sdf_raster

