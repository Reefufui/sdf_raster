#pragma once

#include <filesystem>
#include <string>

#include "camera.hpp"

namespace sdf_raster {

struct Settings {
    Camera camera {};
    std::string scene_name {"lowpoly_bunny"};
    std::filesystem::path scene_path {"./assets/lowpoly_bunny.octree"};

    bool frustum_culling = true;
    bool occlusion_culling = true;

    bool frustum_view = false;
    bool disabled_cursor = true;

    bool show_camera_window = false;
    bool show_occlusion_window = false;
};

struct Stats {
    uint32_t active_leafs_count = 0;
};

void dump_state (const Settings& settings, const std::string& filename);
void load_state (Settings& settings, const std::string& filename);

}

