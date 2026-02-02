#pragma once

#include "camera.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init (int a_width, int a_height, SdfOctree&& a_sdf_octree, size_t a_leaf_memory_limit) = 0;
    virtual void render (const Camera& a_camera) = 0;
    virtual void resize (int a_width, int a_height) = 0;
    virtual void shutdown () = 0;
    virtual void update_push_constants (const Camera& a_camera) = 0;
};

} // namespace sdf_raster

