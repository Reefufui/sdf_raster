// scenes/obj/obj.hpp
#pragma once

#include "scenes/model_state.hpp"
#include "scenes/base/model.hpp"

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

struct ObjModelData {
    std::vector <Vertex> vertices;
    std::vector <uint32_t> indices;
};

class ObjModel : public Model {
public:
    ObjModel () = default;
    ~ObjModel () override;

    bool load (const std::filesystem::path& path) override;
    ModelState& get_state () override;
    void set_state (const ModelState& model_state) override;
    std::span <const DrawMethod> get_available_draw_methods () const override;
    void invalidate_cache () override {}
    size_t get_memory_size () const override;

    const ObjModelData& get_model_data () const;

private:
    ObjModelData data;
    static inline constexpr std::array <DrawMethod, 2> available_methods = {{
        DrawMethod::Explicit, DrawMethod::ExplicitDeferred
    }};
};

} // namespace sdf_raster

