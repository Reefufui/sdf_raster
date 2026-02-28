#pragma once

namespace sdf_raster {

struct Settings;
struct SdfOctree;

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init (SdfOctree&& a_sdf_octree) = 0;
    virtual void render (const Settings& settings) = 0;
    virtual void shutdown () = 0;
    virtual void toggle_frustum_buffer (Settings& settings) = 0; // TODO: refactoring
};

} // namespace sdf_raster

