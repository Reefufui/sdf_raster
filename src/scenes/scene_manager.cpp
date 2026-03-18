#include "scenes/scene_manager.hpp"

namespace sdf_raster {

void SceneManager::load_scene (const std::filesystem::path& path) {
    const std::string extension = path.extension ().string ();

    std::lock_guard lock (m_mutex);

    auto factory_it = this->m_factory_registry.find (extension);
    if (factory_it == this->m_factory_registry.end ()) {
        LOG_ERROR ("[{}] No scene type registered for extension '{}'. Cannot load {}.", SCENE_MANAGER_NAME, extension, path.string ());
        return;
    }

    auto it = this->m_scenes.find (path);
    if (it != this->m_scenes.end () && !std::holds_alternative <std::monostate> (it->second.data)) {
        LOG_INFO ("[{}] Scene {} is already loaded or is loading.", SCENE_MANAGER_NAME, path.string ());
        return;
    }

    LOG_INFO ("[{}] Starting background loading for scene: {}", SCENE_MANAGER_NAME, path.string ());

    auto factory_func = factory_it->second;

    this->m_scenes [path].data = std::async (std::launch::async
        , [path_copy = path, factory = std::move (factory_func)] () -> std::unique_ptr <Scene> {
            std::unique_ptr <Scene> scene = factory ();
            if (!scene) {
                return nullptr;
            }

            if (scene->load (path_copy)) {
                return scene;
            }

            return nullptr;
        }
    );

    this->m_scenes [path].state.path = path;
}

Scene* SceneManager::get_scene (const std::filesystem::path& path) {
    std::lock_guard lock (m_mutex);

    auto it = this->m_scenes.find (path);
    if (it == this->m_scenes.end ()) {
        return nullptr;
    }

    ManagedScene& managed_scene = it->second;

    if (std::holds_alternative <std::future <std::unique_ptr <Scene>>> (managed_scene.data)) {
        LOG_INFO ("Scene {} is loading, waiting for it to finish...", path.string ());
        auto& future = std::get <std::future <std::unique_ptr <Scene>>> (managed_scene.data);

        try {
            std::unique_ptr <Scene> scene_ptr = future.get ();

            if (scene_ptr) {
                managed_scene.state = scene_ptr->get_state (); 
                managed_scene.data = std::move (scene_ptr);
            } else {
                LOG_ERROR ("Error loading scene: {}", path.string ());
                managed_scene.data = std::monostate {};
                return nullptr;
            }
        } catch (const std::exception& e) {
            LOG_ERROR ("Exception during scene loading: {}", e.what ());
            managed_scene.data = std::monostate {};
            return nullptr;
        }
    }

    if (std::holds_alternative <std::unique_ptr <Scene>> (managed_scene.data)) {
        return std::get <std::unique_ptr <Scene>> (managed_scene.data).get ();
    }

    return nullptr;
}

void SceneManager::unload_scene (const std::filesystem::path& path) {
    std::lock_guard lock (m_mutex);

    auto it = this->m_scenes.find (path);
    if (it != this->m_scenes.end ()) {
        if (std::holds_alternative <std::unique_ptr <Scene>> (it->second.data)) {
            LOG_INFO ("Unloading scene data for: {}. State will be cached.", path.string ());
            it->second.data = std::monostate {};
        }
    }
}

std::optional <SceneState> SceneManager::get_cached_state (const std::filesystem::path& path) const {
    std::lock_guard lock (m_mutex);

    auto it = this->m_scenes.find (path);
    if (it != this->m_scenes.end ()) {
        return it->second.state;
    }

    return std::nullopt;
}

} // sdf_raster

