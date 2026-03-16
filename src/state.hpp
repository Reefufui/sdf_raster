#pragma once

#include <filesystem>
#include <string>

#include "camera.hpp"

namespace sdf_raster {

struct Settings {
    int window_width = 1980;
    int window_height = 1080;
    bool window_maximized = true;

    Camera camera {};
    std::string scene_name {"lowpoly_bunny"};
    std::filesystem::path scene_path {"./assets/lowpoly_bunny.octree"}; // TODO: no default scene
    std::filesystem::path scenes_directory {std::filesystem::current_path ()};
    
    int octree_depth;
    int cpu_traversed = 3;
    int gpu_descend = 5;
    int max_lod = 8;

    bool use_mesh_shading = true;

    int frustum_culling_level = 16;
    int occlusion_culling_level = 16;
    bool color_leafs = true;
    bool frustum_view = false;
    bool disabled_cursor = true;

    bool show_ui = true;
    bool show_camera_window = false;
    bool show_renderer_window = false;
};

struct Stats {
    uint32_t active_leafs_count = 0;
    uint32_t active_roots_count = 0;
    int lod = 0;
    float distance = 0.f;
};

void dump_state (const Settings& settings, const std::string& filename);
void load_state (Settings& settings, const std::string& filename);

}

