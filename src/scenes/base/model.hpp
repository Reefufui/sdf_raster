// scenes/base/model.hpp
#pragma once

#include "scenes/model_state.hpp"

#include <filesystem>
#include <span>

namespace sdf_raster {

class Model {
public:
    virtual ~Model () = default;
    virtual bool load (const std::filesystem::path& path) = 0;
    virtual void set_state (const ModelState& model_state) = 0;
    virtual ModelState& get_state () = 0;
    virtual std::span <const DrawMethod> get_available_draw_methods () const = 0;
    virtual size_t get_memory_size () const = 0;

    DrawMethod get_current_draw_method () const;
    void set_current_draw_method (DrawMethod method);

    virtual void invalidate_cache () = 0;
    LiteMath::float4x4 get_model_matrix () const;

protected:
    void apply_state (const ModelState& model_state);
    ModelState state;
    size_t current_draw_method_index = 0;
};

} // sdf_raster

