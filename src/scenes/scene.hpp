#pragma once

#include "scenes/renderable_item.hpp"
#include "scenes/base/model.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <map>

namespace sdf_raster {
class ModelManager;

struct RenderBatch {
    std::string mesh_id;
    std::shared_ptr <Model> model;
    std::vector <RenderableItem> items;
};

struct RenderGroup {
    DrawMethod draw_method;
    std::vector <RenderBatch> batches;
};

class Scene {
public:
    Scene () = default;
    ~Scene () = default;

    bool load (const std::filesystem::path& path, ModelManager& model_manager);

    const std::vector <RenderGroup>& get_groups () const { return groups; }
    const Camera& get_camera () const { return camera; };
    Camera& get_camera_ref () { return camera; };

    const std::string& get_name () const { return this->name; }

private:
    Camera camera;
    std::vector <RenderGroup> groups;
    std::string name;
};

} // namespace sdf_raster
