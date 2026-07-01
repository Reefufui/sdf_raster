// scenes/scomtree/scomtree.hpp
#pragma once

#include "data/mesh.hpp"
#include "scenes/model_state.hpp"
#include "scenes/base/model.hpp"
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

class SComTreeModel : public Model {
public:
    bool load (const std::filesystem::path& path) override;

    ModelState& get_state () override;

    void set_state (const ModelState& model_state) override;

    std::span <const DrawMethod> get_available_draw_methods () const override;

    size_t get_memory_size () const override;

    ~SComTreeModel () override;

    const SComTree& get_octree_data () const;

    std::vector <SComTreeStackElement> collect_visible_subtrees (const FrustumGeometry& frustum) const;

    void dump_as_json (const std::filesystem::path& path) const;

    Mesh generate_mesh () const;

    Mesh generate_voxel_mesh () const;

protected:
    void invalidate_cache () override;

private:
    SComTree data;

    std::vector <SComTreeStackElement> cached_all_subtrees;
    static inline constexpr std::array <DrawMethod, 4> available_methods = {{
        DrawMethod::SComTreeComputeDeferred, DrawMethod::SComTreeCompute,
        DrawMethod::SComTreeMesh, DrawMethod::SComTreeMeshDeferred
    }};
};

} // sdf_raster

