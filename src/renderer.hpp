#pragma once

namespace sdf_raster {

struct SdfOctree;
struct Settings;
struct Stats;

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init () = 0;
    virtual void update (const SdfOctree& scene, Settings& settings) = 0;
    virtual void render (const Settings& settings) = 0;
    virtual void shutdown () = 0;
    virtual const Stats& get_stats () = 0;
};

} // namespace sdf_raster

