#pragma once

#include "scenes/renderable_item.hpp"
#include "scenes/base/model.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <map>

namespace sdf_raster {
class ModelManager;

struct RenderBatch {
    std::string mesh_id;
    std::shared_ptr <Model> model;
    std::vector <RenderableItem> items;
};

struct RenderGroup {
    DrawMethod draw_method;
    std::vector <RenderBatch> batches;
};

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

class Scene {
public:
    Scene () = default;
    ~Scene () = default;

    bool load (const std::filesystem::path& path, ModelManager& model_manager);

    const std::vector <RenderGroup>& get_groups () const { return groups; }
    const Camera& get_camera () const { return camera; };
    Camera& get_camera_ref () { return camera; };
    const LightingSettings& get_lighting_settings () const { return lighting_settings; };
    LightingSettings& get_lighting_settings_ref () { return lighting_settings; };

    const std::string& get_name () const { return this->name; }

private:
    LightingSettings lighting_settings;
    Camera camera;
    std::vector <RenderGroup> groups;
    std::string name;
};

} // namespace sdf_raster
