#pragma once

#include "camera.hpp"

#include <filesystem>
#include <string>

namespace sdf_raster {

enum class DrawMethod : uint8_t { None, Explicit, ExplicitDeferred, ImplicitCompute, ImplicitMesh };

struct SceneState {
    Camera camera {};
    DrawMethod draw_method = DrawMethod::None;
    std::string name {"N/A"};
    std::filesystem::path path {};
    int octree_depth {16};
    int cpu_traversed {3};
    int gpu_descend {5}; // NOTE: deprecated
    int max_lod {16};
    int frustum_culling_level {16};
    int occlusion_culling_level {16};
};

} // sdf_raster

