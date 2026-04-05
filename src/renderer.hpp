#pragma once

#include <vk_include.h>

#include <mutex>
#include <queue>

namespace sdf_raster {

struct SceneState;
struct SdfOctree;
struct Settings;
struct Stats;

class Renderer;
using RenderCommand = std::function <void (Renderer* renderer)>;

class Renderer {
public:
    virtual ~Renderer () = default;

    virtual void init () = 0;
    virtual void update (uint32_t frame_index, Settings& settings, SceneState& scene_state) = 0;
    virtual void render (VkCommandBuffer cmd_buff) = 0;
    virtual void shutdown (Settings& settings) = 0;
    virtual void process_commands (std::queue <RenderCommand>& commands, std::mutex& mutex) = 0;
    virtual const Stats& get_stats () = 0;
};

} // namespace sdf_raster

