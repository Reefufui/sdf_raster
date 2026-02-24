#pragma once

#include "camera.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init (SdfOctree&& a_sdf_octree) = 0;
    virtual void render (const Camera& a_camera) = 0;
    virtual void shutdown () = 0;
    virtual void toggle_frustum_buffer (Camera& camera) = 0; // TODO: refactoring
};

} // namespace sdf_raster

