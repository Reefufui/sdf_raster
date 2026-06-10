// scenes/octree/octree.hpp
#pragma once

#include "scenes/model_state.hpp"
#include "scenes/base/model.hpp"

#include <filesystem>

struct FrustumGeometry;
struct NodeContext;
struct SdfOctreeNode;

namespace sdf_raster {

struct SdfOctree {
    std::string name; // TODO: remove
    std::vector <SdfOctreeNode> nodes;
};

class SdfOctreeModel : public Model {
public:
    bool load (const std::filesystem::path& path) override;

    ModelState& get_state () override;

    void set_state (const ModelState& model_state) override;

    std::span <const DrawMethod> get_available_draw_methods () const override;

    size_t get_memory_size () const override;

    ~SdfOctreeModel () override;

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

