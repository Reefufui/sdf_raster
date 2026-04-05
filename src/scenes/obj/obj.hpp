#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/scene.hpp"

#include "shader_common.hpp"

#include <LiteMath.h>

#include <filesystem>
#include <vector>

namespace sdf_raster {

struct Vertex {
    LiteMath::float4 position;
    LiteMath::float4 normal;
    LiteMath::float4 color;
};

struct ObjModel {
    std::vector <Vertex> vertices;
    std::vector <uint32_t> indices;
};

class ObjScene : public Scene {
public:
    ObjScene () = default;
    ~ObjScene () override;

    bool load (const std::filesystem::path& path) override;
    SceneState& get_state () override;
    void set_state (const SceneState& scene_state) override;

    const ObjModel& get_model_data () const;

private:
    SceneState state;
    ObjModel data;
};

} // namespace sdf_raster

