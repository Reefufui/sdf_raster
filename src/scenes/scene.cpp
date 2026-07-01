// scenes/scene.cpp
#include "scene.hpp"

#include "logger.hpp"
#include "scenes/base/model_manager.hpp"
#include "state_json.hpp"

#include <fstream>
#include <map>

namespace sdf_raster {

bool Scene::load (const std::filesystem::path& path, ModelManager& model_manager) {
    this->name = path.stem ().string ();
    std::ifstream file (path);
    if (!file.is_open ()) {
        LOG_ERROR ("Failed to open scene file: {}", path.string ());
        return false;
    }

    nlohmann::json json_data;
    try {
        file >> json_data;
    } catch (const std::exception& e) {
        LOG_ERROR ("Failed to parse scene JSON: {}", e.what ());
        return false;
    }

    this->groups.clear ();
    std::map <std::string, std::shared_ptr <Model>> mesh_cache;

    if (json_data.contains ("meshes")) {
        for (const auto& mesh_json : json_data ["meshes"]) {
            std::string id = mesh_json ["id"].get <std::string> ();
            std::filesystem::path mesh_path = mesh_json ["path"].get <std::filesystem::path> ();

            auto absolute_mesh_path = path.parent_path () / mesh_path;

            auto model = model_manager.create_model (absolute_mesh_path);
            if (model) {
                mesh_cache [id] = model;
            } else {
                LOG_ERROR ("Failed to load model: {}", absolute_mesh_path.string ());
            }
        }
    }

    struct InstanceInfo {
        std::string mesh_id;
        std::shared_ptr <Model> model;
        LiteMath::float4x4 transform;
    };
    std::vector <InstanceInfo> instances;

    if (json_data.contains ("instances")) {
        for (const auto& instance_json : json_data ["instances"]) {
            std::string mesh_id = instance_json ["mesh_id"].get <std::string> ();
            if (mesh_cache.count (mesh_id)) {
                InstanceInfo info;
                info.mesh_id = mesh_id;
                info.model = mesh_cache [mesh_id];
                info.transform = LiteMath::float4x4 ();

                if (instance_json.contains ("transform")) {
                    instance_json ["transform"].get_to (info.transform);
                }
                instances.push_back (info);
            } else {
                LOG_WARN ("Mesh ID not found: {}", mesh_id);
            }
        }
    }

    std::map <DrawMethod, std::map <std::string, RenderBatch>> grouped_data;
    for (auto& inst : instances) {
        DrawMethod method = inst.model->get_current_draw_method ();
        auto& batch = grouped_data [method] [inst.mesh_id];
        batch.mesh_id = inst.mesh_id;
        batch.model = inst.model;
        batch.items.push_back ({inst.transform});
    }

    for (auto& [method, batches_map] : grouped_data) {
        RenderGroup group;
        group.draw_method = method;
        for (auto& [id, batch] : batches_map) {
            group.batches.push_back (std::move (batch));
        }
        this->groups.push_back (std::move (group));
    }

    // NOTE: Maybe draw_method should be bitset of flags for the ease of sorting.
    std::sort (this->groups.begin (), this->groups.end (), [](const RenderGroup& a, const RenderGroup& b) {
        std::string a_name = std::string (draw_method_name (a.draw_method));
        std::string b_name = std::string (draw_method_name (b.draw_method));
        bool a_is_deferred = a_name.find ("Deferred") != std::string::npos;
        bool b_is_deferred = b_name.find ("Deferred") != std::string::npos;
        if (a_is_deferred != b_is_deferred) {
            return a_is_deferred;
        }
        return a_name < b_name;
    });

    size_t total_instances = 0;
    for (const auto& g : groups) {
        for (const auto& b : g.batches) total_instances += b.items.size ();
    }

    LOG_INFO ("Scene loaded: {} methods, {} instances", groups.size (), total_instances);
    return !this->groups.empty ();
}

} // namespace sdf_raster
