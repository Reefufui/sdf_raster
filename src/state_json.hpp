// settings_json.hpp
#pragma once

#include "state.hpp"

#include <nlohmann/json.hpp>
#include <LiteMath.h>

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

template <>
struct adl_serializer <sdf_raster::SceneState> {
    static void to_json (json& j, const sdf_raster::SceneState& scene_state) {
        j = json {
            {"camera", scene_state.camera},
            {"draw_method", scene_state.draw_method},
            {"name", scene_state.name},
            {"path", scene_state.path},
            {"octree_depth", scene_state.octree_depth},
            {"cpu_traversed", scene_state.cpu_traversed},
            {"gpu_descend", scene_state.gpu_descend},
            {"max_lod", scene_state.max_lod},
            {"frustum_culling_level", scene_state.frustum_culling_level},
            {"occlusion_culling_level", scene_state.occlusion_culling_level}
        };
    }

    static void from_json (const json& j, sdf_raster::SceneState& scene_state) {
        j.at ("camera").get_to (scene_state.camera);
        j.at ("draw_method").get_to (scene_state.draw_method);
        j.at ("name").get_to (scene_state.name);
        j.at ("path").get_to (scene_state.path);
        j.at ("octree_depth").get_to (scene_state.octree_depth);
        j.at ("cpu_traversed").get_to (scene_state.cpu_traversed);
        j.at ("gpu_descend").get_to (scene_state.gpu_descend);
        j.at ("max_lod").get_to (scene_state.max_lod);
        j.at ("frustum_culling_level").get_to (scene_state.frustum_culling_level);
        j.at ("occlusion_culling_level").get_to (scene_state.occlusion_culling_level);
    }
};

template <typename T>
struct adl_serializer <std::optional <T>> {
    static void to_json (json& j, const std::optional <T>& opt) {
        if (opt.has_value ()) {
            j = *opt;
        } else {
            j = nullptr;
        }
    }

    static void from_json (const json& j, std::optional <T>& opt) {
        if (j.is_null ()) {
            opt = std::nullopt;
        } else {
            opt = j.get <T>();
        }
    }
};

} // namespace nlohmann

namespace sdf_raster {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
        LightingSettings,
        light_pos,
        light_color,
        fog_color,
        ambient_strength,
        specular_strength,
        shininess,
        depth_threshold,
        fog_start,
        fog_end
    )

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
        Settings,
        window_width,
        window_height,
        window_maximized,
        scenes_directory,
        color_leafs,
        show_ui,
        show_camera_window,
        show_renderer_window,
        lighting
    )

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
        SessionState,
        settings,
        current_scene_path,
        scene_states
    )
} // namespace sdf_raster

