// state.hpp
#pragma once

#include "scenes/model_state.hpp"

#include <LiteMath.h>

#include <filesystem>
#include <string>

namespace sdf_raster {

struct LightingSettings {
    LiteMath::float3 light_pos   = {5.f, 5.f, 5.f};
    LiteMath::float3 light_color = {1.f, 1.f, 1.f};
    LiteMath::float3 fog_color   = {0.25f, 0.25f, 0.25f};
    LiteMath::float4 clear_color = {0.25f, 0.25f, 0.25f, 1.f};

    float ambient_strength  = 0.1f;
    float specular_strength = 0.4f;
    float shininess         = 64.0f;
    float depth_threshold   = 0.0001f;
    float fog_start         = 0.999f;
    float fog_end           = 1.0f;
};

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

    LightingSettings lighting;
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

