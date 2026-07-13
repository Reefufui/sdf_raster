// state_json.hpp
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
struct adl_serializer <LiteMath::float4x4> {
    static void to_json (json& j, const LiteMath::float4x4& mat) {
        j = json::array();
        for (int i = 0; i < 4; i++) {
            j.push_back(mat.col(i).x);
            j.push_back(mat.col(i).y);
            j.push_back(mat.col(i).z);
            j.push_back(mat.col(i).w);
        }
    }

    static void from_json (const json& j, LiteMath::float4x4& mat) {
        for (int i = 0; i < 4; i++) {
            LiteMath::float4 r;
            r.x = j.at(i * 4 + 0).get<float>();
            r.y = j.at(i * 4 + 1).get<float>();
            r.z = j.at(i * 4 + 2).get<float>();
            r.w = j.at(i * 4 + 3).get<float>();
            mat.set_row(i, r);
        }
    }
};

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
struct adl_serializer <LiteMath::float4> {
    static void to_json (json& j, const LiteMath::float4& vec) {
        j = {vec.x, vec.y, vec.z, vec.w};
    }

    static void from_json (const json& j, LiteMath::float4& vec) {
        j.at (0).get_to (vec.x);
        j.at (1).get_to (vec.y);
        j.at (2).get_to (vec.z);
        j.at (3).get_to (vec.w);
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
struct adl_serializer <sdf_raster::Keyframe> {
    static void to_json (json& j, const sdf_raster::Keyframe& k) {
        j = json {
            {"position", k.position}
            , {"orientation", k.orientation}
        };
    }

    static void from_json (const json& j, sdf_raster::Keyframe& k) {
        j.at ("position").get_to (k.position);
        j.at ("orientation").get_to (k.orientation);
    }
};

template <>
struct adl_serializer <sdf_raster::Trajectory> {
    static void to_json (json& j, const sdf_raster::Trajectory& t) {
        j = json {
            {"name", t.name}
            , {"duration", t.duration}
            , {"keyframes", t.keyframes}
        };
    }

    static void from_json (const json& j, sdf_raster::Trajectory& t) {
        j.at ("name").get_to (t.name);
        j.at ("duration").get_to (t.duration);
        j.at ("keyframes").get_to (t.keyframes);
    }
};

template <>
struct adl_serializer <sdf_raster::Camera> {
    static void to_json (json& j, const sdf_raster::Camera& cam) {
        auto camera_target = cam.position + cam.front;
        j = json {
            {"position", cam.position}
            , {"orientation", cam.orientation}
            , {"fov_y", cam.fov_y}
            , {"movement_speed", cam.movement_speed}
            , {"mouse_sensitivity", cam.mouse_sensitivity}
            , {"near_plane", cam.near_plane}
            , {"far_plane", cam.far_plane}
            , {"aspect_ratio", cam.aspect_ratio}
            , {"trajectories", cam.trajectories}
            , {"LiteRT", json {
                {"camera_pos", cam.position},
                {"camera_target", camera_target},
                {"camera_up", cam.up}
            }}
        };
    }

    static void from_json (const json& j, sdf_raster::Camera& cam) {
        j.at ("position").get_to (cam.position);
        if (j.contains ("orientation")) {
            j.at ("orientation").get_to (cam.orientation);
        }
        j.at ("fov_y").get_to (cam.fov_y);
        j.at ("movement_speed").get_to (cam.movement_speed);
        j.at ("mouse_sensitivity").get_to (cam.mouse_sensitivity);
        j.at ("near_plane").get_to (cam.near_plane);
        j.at ("far_plane").get_to (cam.far_plane);
        j.at ("aspect_ratio").get_to (cam.aspect_ratio);

        if (j.contains ("trajectories")) {
            j.at ("trajectories").get_to (cam.trajectories);
        }

        cam.update ();
    }
};

template <>
struct adl_serializer <sdf_raster::ModelState> {
    static void to_json (json& j, const sdf_raster::ModelState& model_state) {
        j = json {
            {"camera", model_state.camera},
            {"draw_method", model_state.draw_method},
            {"name", model_state.name},
            {"path", model_state.path},
            {"octree_depth", model_state.octree_depth},
            {"cpu_traversed", model_state.cpu_traversed},
            {"gpu_descend", model_state.gpu_descend},
            {"max_lod", model_state.max_lod},
            {"frustum_culling_level", model_state.frustum_culling_level},
            {"occlusion_culling_level", model_state.occlusion_culling_level},
            {"lod_mode", model_state.lod_mode},
            {"fixed_lod", model_state.fixed_lod},
            {"octree_root_center", model_state.octree_root_center},
            {"lod_threshold_pixels", model_state.lod_threshold_pixels},
            {"min_lod", model_state.min_lod},
            {"lod_aggressivity", model_state.lod_aggressivity}
        };
    }

    static void from_json (const json& j, sdf_raster::ModelState& model_state) {
        j.at ("camera").get_to (model_state.camera);
        j.at ("draw_method").get_to (model_state.draw_method);
        j.at ("name").get_to (model_state.name);
        j.at ("path").get_to (model_state.path);
        j.at ("octree_depth").get_to (model_state.octree_depth);
        j.at ("cpu_traversed").get_to (model_state.cpu_traversed);
        j.at ("gpu_descend").get_to (model_state.gpu_descend);
        j.at ("max_lod").get_to (model_state.max_lod);
        j.at ("frustum_culling_level").get_to (model_state.frustum_culling_level);
        j.at ("occlusion_culling_level").get_to (model_state.occlusion_culling_level);

        j.at ("lod_mode").get_to (model_state.lod_mode);

        if (j.contains ("fixed_lod")) {
            j.at ("fixed_lod").get_to (model_state.fixed_lod);
        }

        j.at ("octree_root_center").get_to (model_state.octree_root_center);
        j.at ("lod_threshold_pixels").get_to (model_state.lod_threshold_pixels);

        if (j.contains ("min_lod")) {
            j.at ("min_lod").get_to (model_state.min_lod);
        }
        if (j.contains ("lod_aggressivity")) {
            j.at ("lod_aggressivity").get_to (model_state.lod_aggressivity);
        }
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
    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(
    //     LightingSettings,
    //     light_pos,
    //     light_color,
    //     fog_color,
    //     clear_color,
    //     ambient_strength,
    //     specular_strength,
    //     shininess,
    //     depth_threshold,
    //     fog_start,
    //     fog_end
    // )

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
        Settings,
        window_width,
        window_height,
        window_maximized,
        scenes_directory,
        color_leafs,
        show_ui,
        show_camera_window,
        show_renderer_window
        // lighting
    )

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE (
        SessionState,
        settings,
        current_model_path,
        model_states
    )
} // namespace sdf_raster

