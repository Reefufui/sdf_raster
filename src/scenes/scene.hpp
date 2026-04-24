#pragma once

#include "scenes/scene_state.hpp"

#include <filesystem>
#include <span>

namespace sdf_raster {

class Scene {
public:
    virtual ~Scene () = default;
    virtual bool load (const std::filesystem::path& path) = 0;
    virtual void set_state (const SceneState& scene_state) = 0;
    virtual SceneState& get_state () = 0;
    virtual std::span <const DrawMethod> get_available_draw_methods () const = 0;

    DrawMethod get_current_draw_method () const;
    void set_current_draw_method (DrawMethod method);

    virtual void invalidate_cache () = 0;

protected:
    void apply_state (const SceneState& scene_state);
    SceneState state;
    size_t current_draw_method_index = 0;
};

} // sdf_raster

