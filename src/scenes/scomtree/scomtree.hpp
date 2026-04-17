#pragma once

#include "mesh.hpp"
#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"
#include "scenes/scomtree/header.hpp"

#include <cstdint>
#include <vector>

namespace sdf_raster {

struct SComTree {
    std::string name;

    Header header; // TODO: switch to SComTreeHeader from "shader_common.hpp"
    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;
};

class SComTreeScene : public Scene {
public:
    bool load (const std::filesystem::path& path) override;

    SceneState& get_state () override;

    void set_state (const SceneState& scene_state) override;

    ~SComTreeScene () override;

    const SComTree& get_octree_data () const;

    void dump_as_json (const std::filesystem::path& path) const;

    Mesh generate_mesh () const;

    Mesh generate_voxel_mesh () const;

private:
    SceneState state;
    SComTree data;
};

} // sdf_raster

