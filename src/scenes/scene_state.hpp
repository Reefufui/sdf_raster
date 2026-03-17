#pragma once

#include "camera.hpp"

#include <filesystem>
#include <string>

namespace sdf_raster {

struct SceneState {
    Camera camera {};
    std::string scene_name {"N/A"};
    std::filesystem::path scene_path {};
    int octree_depth;
    int cpu_traversed;
    int gpu_descend;
    int max_lod;
    int frustum_culling_level;
    int occlusion_culling_level;
};

} // sdf_raster

