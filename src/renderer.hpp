#pragma once

#include "vk_include.h"

namespace sdf_raster {

struct SdfOctree;
struct Settings;
struct Stats;

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init () = 0;
    virtual void update (uint32_t frame_index, const SdfOctree& scene, Settings& settings) = 0;
    virtual void render (VkCommandBuffer cmd_buff) = 0;
    virtual void shutdown (Settings& settings) = 0;
    virtual const Stats& get_stats () = 0;
};

} // namespace sdf_raster

