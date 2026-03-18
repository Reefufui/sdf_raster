#pragma once

#include "camera.hpp"

#include <filesystem>
#include <string>

namespace sdf_raster {

struct SceneState {
    Camera camera {};
    std::string name {"lowpoly_bunny"};
    std::filesystem::path path {"./../assets/lowpoly_bunny.octree"};
    int octree_depth {16};
    int cpu_traversed {3};
    int gpu_descend {5};
    int max_lod {10};
    int frustum_culling_level {10};
    int occlusion_culling_level {10};
};

} // sdf_raster

