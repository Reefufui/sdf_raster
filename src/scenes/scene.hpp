#pragma once

#include "scenes/scene_state.hpp"

#include <filesystem>

namespace sdf_raster {

class Scene {
public:
    virtual ~Scene () = default;
    virtual bool load (const std::filesystem::path& path) = 0;
    virtual SceneState get_state () const = 0;
};

} // sdf_raster

