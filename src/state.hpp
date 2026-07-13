// state.hpp
#pragma once

#include "scenes/model_state.hpp"

#include <LiteMath.h>

#include <filesystem>
#include <string>

namespace sdf_raster {

struct Settings {
    int window_width = 1980;
    int window_height = 1080;
    bool window_maximized = true;

    std::filesystem::path scenes_directory {std::filesystem::current_path ()};

    uint color_leafs = 0;
    bool frustum_view = false;
    bool disabled_cursor = true;

    bool show_ui = true;
    bool show_camera_window = false;
    bool show_renderer_window = false;
    bool show_scene_inspector = false;
    bool animate_rotation = false;
};

struct SessionState {
    Settings settings;
    std::optional <std::filesystem::path> current_model_path;
    std::map <std::filesystem::path, ModelState> model_states;
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

