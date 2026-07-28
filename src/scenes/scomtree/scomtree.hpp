// scenes/scomtree/scomtree.hpp
#pragma once

#include "data/mesh.hpp"
#include "scenes/scene_state.hpp"
#include "scenes/base/scene.hpp"
#include "scenes/scomtree/header.hpp"

#include <cstdint>
#include <vector>

struct SComTreeStackElement;

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

    std::span <const DrawMethod> get_available_draw_methods () const override;

    size_t get_memory_size () const override;

    ~SComTreeScene () override;

    const SComTree& get_octree_data () const;

    std::vector <SComTreeStackElement> collect_visible_subtrees (const FrustumGeometry& frustum) const;

    std::vector <SComTreeStackElement> collect_all_subtrees () const;

    void dump_as_json (const std::filesystem::path& path) const;

    Mesh generate_mesh () const;

    Mesh generate_voxel_mesh () const;

public:
    void invalidate_cache () override;

private:
    SComTree data;

    std::vector <SComTreeStackElement> cached_all_subtrees;
    static inline constexpr std::array <DrawMethod, 4> available_methods = {{
        DrawMethod::SComTreeCompute, DrawMethod::SComTreeComputeDeferred,
        DrawMethod::SComTreeMesh, DrawMethod::SComTreeMeshDeferred
    }};
};

} // sdf_raster

