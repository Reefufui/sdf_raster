// scenes/obj/obj.hpp
#pragma once

#include "scenes/scene_state.hpp"
#include "scenes/base/scene.hpp"

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
    std::span <const DrawMethod> get_available_draw_methods () const override;
    void invalidate_cache () override {}
    size_t get_memory_size () const override;

    const ObjModel& get_model_data () const;

private:
    ObjModel data;
    static inline constexpr std::array <DrawMethod, 2> available_methods = {{
        DrawMethod::Explicit, DrawMethod::ExplicitDeferred
    }};
};

} // namespace sdf_raster

