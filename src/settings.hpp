#pragma once

#include "camera.hpp"

namespace sdf_raster {

struct Settings {
    Camera camera;
    bool frustum_culling = true;
    bool occlusion_culling = true;
};

}

