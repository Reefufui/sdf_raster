// settings_json.hpp
#pragma once

#include "state.hpp"
#include "nlohmann/json.hpp"
#include "LiteMath.h"

#include <filesystem>
#include <string>

using json = nlohmann::json;

namespace nlohmann {

template <>
struct adl_serializer <LiteMath::float3> {
    static void to_json (json& j, const LiteMath::float3& vec) {
        j = {vec.x, vec.y, vec.z};
    }

    static void from_json (const json& j, LiteMath::float3& vec) {
        j.at (0).get_to (vec.x);
        j.at (1).get_to (vec.y);
        j.at (2).get_to (vec.z);
    }
};

template <>
struct adl_serializer <std::filesystem::path> {
    static void to_json (json& j, const std::filesystem::path& p) {
        j = p.string ();
    }

    static void from_json (const json& j, std::filesystem::path& p) {
        p = j.get <std::string> ();
    }
};

template <>
struct adl_serializer <sdf_raster::Camera> {
    static void to_json (json& j, const sdf_raster::Camera& cam) {
        j = json {
            {"position", cam.position},
            {"yaw_angle", cam.yaw_angle},
            {"pitch_angle", cam.pitch_angle},
            {"fov_y", cam.fov_y},
            {"movement_speed", cam.movement_speed},
            {"mouse_sensitivity", cam.mouse_sensitivity},
            {"near_plane", cam.near_plane},
            {"far_plane", cam.far_plane},
            {"aspect_ratio", cam.aspect_ratio}
        };
    }

    static void from_json (const json& j, sdf_raster::Camera& cam) {
        j.at ("position").get_to (cam.position);
        j.at ("yaw_angle").get_to (cam.yaw_angle);
        j.at ("pitch_angle").get_to (cam.pitch_angle);
        j.at ("fov_y").get_to (cam.fov_y);
        j.at ("movement_speed").get_to (cam.movement_speed);
        j.at ("mouse_sensitivity").get_to (cam.mouse_sensitivity);
        j.at ("near_plane").get_to (cam.near_plane);
        j.at ("far_plane").get_to (cam.far_plane);
        j.at ("aspect_ratio").get_to (cam.aspect_ratio);
        cam.update ();
    }
};

} // namespace nlohmann

namespace sdf_raster {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
        Settings,
        window_width,
        window_height,
        window_maximized,
        camera,
        scene_name,
        scene_path,
        scenes_directory,
        cpu_traversed,
        max_lod,
        frustum_culling_level,
        occlusion_culling_level,
        color_leafs,
        show_ui,
        show_camera_window,
        show_renderer_window,
        use_mesh_shading
    )
} // namespace sdf_raster

