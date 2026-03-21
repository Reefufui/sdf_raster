#pragma once

#include <filesystem>
#include <string>

#include "scenes/scene_state.hpp"

namespace sdf_raster {

struct Settings {
    int window_width = 1980;
    int window_height = 1080;
    bool window_maximized = true;

    SceneState scene_state; // TODO: remove

    std::filesystem::path scenes_directory {std::filesystem::current_path ()};

    bool use_mesh_shading = true;

    bool color_leafs = true;
    bool frustum_view = false;
    bool disabled_cursor = true;

    bool show_ui = true;
    bool show_camera_window = false;
    bool show_renderer_window = false;
};

struct SessionState {
    Settings settings;
    std::optional <std::filesystem::path> current_scene_path;
    std::map <std::filesystem::path, SceneState> scene_states;
};

void dump_session (const SessionState& session, const std::string& filename);
void load_session (SessionState& session, const std::string& filename);

struct Stats {
    uint32_t active_leafs_count = 0;
    uint32_t active_roots_count = 0;
    int lod = 0;
    float distance = 0.f;
};

}

