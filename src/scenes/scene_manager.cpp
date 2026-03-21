#include "scenes/scene_manager.hpp"

namespace sdf_raster {

SceneManager::SceneManager () {
    this->is_running = true;
    this->worker = std::thread (&SceneManager::worker_thread_loop, this);
}

SceneManager::~SceneManager () {
    this->is_running = false;
    if (this->worker.joinable ()) {
        this->worker.join ();
    }
}

std::map <std::filesystem::path, SceneState> SceneManager::get_all_states () const {
    std::lock_guard lock (this->mutex);
    std::map <std::filesystem::path, SceneState> result;
    for (const auto& [path, managed_scene] : this->scenes) {
        result [path] = managed_scene.state;
    }
    return result;
}

void SceneManager::restore_states (const std::map <std::filesystem::path, SceneState>& states) {
    std::lock_guard lock (this->mutex);

    for (const auto& [path, state] : states) {
        this->scenes [path].state = state;
    }

    LOG_INFO ("[{}] Restored {} scene states.", SCENE_MANAGER_NAME, states.size());
}

void SceneManager::load_scene (const std::filesystem::path& path) {
    const std::string extension = path.extension ().string ();

    std::lock_guard lock (this->mutex);

    auto factory_it = this->factory_registry.find (extension);
    if (factory_it == this->factory_registry.end ()) {
        LOG_ERROR ("[{}] No scene type registered for extension '{}'. Cannot load {}.", SCENE_MANAGER_NAME, extension, path.string ());
        return;
    }

    auto it = this->scenes.find (path);
    if (it != this->scenes.end () && !std::holds_alternative <std::monostate> (it->second.data)) {
        LOG_INFO ("[{}] Scene {} is already loaded or is loading.", SCENE_MANAGER_NAME, path.string ());
        return;
    }

    LOG_INFO ("[{}] Starting background loading for scene: {}", SCENE_MANAGER_NAME, path.string ());

    auto factory_func = factory_it->second;

    this->scenes [path].data = std::async (std::launch::async
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

    this->scenes [path].state.path = path;
}

void SceneManager::set_current_scene (const std::filesystem::path& path) {
    std::lock_guard lock (this->mutex);

    if (this->scenes.find (path) == this->scenes.end ()) {
        LOG_ERROR ("[{}] Cannot set scene '{}' as current: scene is not managed.", SCENE_MANAGER_NAME, path.string ());
        this->current_scene_path.reset ();
        return;
    }

    LOG_INFO ("[{}] Scene '{}' is now set as current.", SCENE_MANAGER_NAME, path.string ());
    this->current_scene_path = path;
}

Scene* SceneManager::get_scene (const std::filesystem::path& path) {
    std::optional <std::pair <SceneEventType, std::filesystem::path>> event_to_fire;
    Scene* scene_ptr_to_return = nullptr;

    {
        std::lock_guard lock (this->mutex);

        auto it = this->scenes.find (path);
        if (it == this->scenes.end ()) {
            return nullptr;
        }

        ManagedScene& managed_scene = it->second;

        if (std::holds_alternative <std::future <std::unique_ptr <Scene>>> (managed_scene.data)) {
            LOG_INFO ("Scene {} is loading, waiting for it to finish...", path.string ());
            auto& future = std::get <std::future <std::unique_ptr <Scene>>> (managed_scene.data);

            if (future.wait_for (std::chrono::seconds (0)) == std::future_status::ready) {
                try {
                    std::unique_ptr <Scene> scene_ptr = future.get ();

                    if (scene_ptr) {
                        managed_scene.state = scene_ptr->get_state ();
                        managed_scene.data = std::move (scene_ptr);
                        scene_ptr_to_return = scene_ptr.get ();
                        event_to_fire = {SceneEventType::LOADED, path};
                    } else {
                        LOG_ERROR ("Error loading scene: {}", path.string ());
                        managed_scene.data = std::monostate {};
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR ("Exception during scene loading: {}", e.what ());
                    managed_scene.data = std::monostate {};
                }
            }
        } else if (std::holds_alternative <std::unique_ptr <Scene>> (managed_scene.data)) {
            return std::get <std::unique_ptr <Scene>> (managed_scene.data).get ();
        }
    }

    if (event_to_fire.has_value()) {
        this->notify (event_to_fire->first, event_to_fire->second);
    }

    return scene_ptr_to_return;
}

Scene* SceneManager::get_current_scene () {
    std::filesystem::path path_to_get;
    {
        std::lock_guard lock (this->mutex);
        if (!this->current_scene_path.has_value ()) {
            return nullptr;
        }
        path_to_get = *this->current_scene_path;
    }

    return this->get_scene (path_to_get);
}

std::optional <std::filesystem::path> SceneManager::get_current_scene_path () {
    return this->current_scene_path;
}

void SceneManager::unload_scene (const std::filesystem::path& path) {
    std::lock_guard lock (this->mutex);

    if (this->current_scene_path && *this->current_scene_path == path) {
        LOG_INFO("[{}] Unloading the current scene. There will be no current scene now.", SCENE_MANAGER_NAME);
        this->current_scene_path.reset ();
    }

    auto it = this->scenes.find (path);
    if (it != this->scenes.end ()) {
        if (std::holds_alternative <std::unique_ptr <Scene>> (it->second.data)) {
            LOG_INFO ("Unloading scene data for: {}. State will be cached.", path.string ());
            it->second.data = std::monostate {};
            this->notify (SceneEventType::UNLOADED, path);
        }
    }
}

std::optional <SceneState> SceneManager::get_state (const std::filesystem::path& path) const {
    std::lock_guard lock (this->mutex);
    return get_state_impl (path);
}

std::optional <SceneState> SceneManager::get_current_state () const {
    std::lock_guard lock (this->mutex);

    if (!this->current_scene_path.has_value ()) {
        return std::nullopt;
    }

    return get_state_impl (*this->current_scene_path);
}

void SceneManager::subscribe (SceneEventCallback callback) {
    this->subscribers.push_back (std::move (callback));
}

[[nodiscard]] std::optional <SceneState> SceneManager::get_state_impl (const std::filesystem::path& path) const {
    auto it = this->scenes.find (path);
    if (it != this->scenes.end ()) {
        return it->second.state;
    }
    return std::nullopt;
}

void SceneManager::notify (SceneEventType type, const std::filesystem::path& path) {
    for (const auto& callback : this->subscribers) {
        if (callback) {
            callback (type, path);
        }
    }
}

void SceneManager::worker_thread_loop () {
    while (this->is_running) {
        std::vector <std::filesystem::path> scenes_to_check;

        {
            std::lock_guard lock (this->mutex);
            for (const auto& [path, managed_scene] : this->scenes) {
                if (std::holds_alternative <std::future <std::unique_ptr <Scene>>> (managed_scene.data)) {
                    scenes_to_check.push_back (path);
                }
            }
        }

        if (!scenes_to_check.empty ()) {
            for (const auto& path : scenes_to_check) {
                this->get_scene (path);
            }
        }

        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
}

} // sdf_raster

