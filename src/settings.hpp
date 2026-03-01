#pragma once

#include "camera.hpp"

namespace sdf_raster {

struct Settings {
    Camera camera;

    bool frustum_culling = true;
    bool occlusion_culling = true;

    bool frustum_view = false;
    bool disabled_cursor = true;
};

struct Stats {
    uint32_t active_leafs_count = 0;
};

}

