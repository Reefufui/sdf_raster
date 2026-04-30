// scenes/base/scene_manager.cpp
#include "scenes/base/scene_manager.hpp"

namespace sdf_raster {

SceneManager::SceneManager () {
}

SceneManager::~SceneManager () {
}

[[nodiscard]] std::vector <std::string> SceneManager::get_registered_extensions () const {
    std::lock_guard lock (this->mutex);

    std::vector <std::string> extensions;
    extensions.reserve (this->factory_registry.size ());
    for (const auto& [extension, factory_func] : this->factory_registry) {
        extensions.push_back (extension);
    }

    return extensions;
}

std::map <std::filesystem::path, SceneState> SceneManager::get_all_states () const {
    std::lock_guard lock (this->mutex);
    std::map <std::filesystem::path, SceneState> result = this->cached_scene_states;
    if (std::holds_alternative <std::shared_ptr <Scene>> (this->managed_scene)) {
        auto& state = std::get <std::shared_ptr <Scene>> (this->managed_scene)->get_state ();
        result [state.path] = state;
    }
    return result;
}

void SceneManager::restore_states (const std::map <std::filesystem::path, SceneState>& states) {
    std::lock_guard lock (this->mutex);

    for (const auto& [path, state] : states) {
        this->cached_scene_states [path] = state;
    }

    LOG_INFO ("[{}] Restored {} scene states.", SCENE_MANAGER_NAME, this->cached_scene_states.size ());
}

void SceneManager::load_scene (const std::filesystem::path& path) {
    std::lock_guard lock (this->mutex);

    if (this->is_loading) {
        LOG_WARN ("[{}] Some scene is currently loading. Cannot load {}.", SCENE_MANAGER_NAME, path.string ());
        return;
    }

    const std::string extension = path.extension ().string ();
    auto factory_it = this->factory_registry.find (extension);
    if (factory_it == this->factory_registry.end ()) {
        LOG_ERROR ("[{}] No scene type registered for extension '{}'. Cannot load {}.", SCENE_MANAGER_NAME, extension, path.string ());
        return;
    }

    std::optional <SceneState> old_state;
    if (auto cache_it = this->cached_scene_states.find (path); cache_it != this->cached_scene_states.end ()) {
        old_state = cache_it->second;
    }

    if (std::holds_alternative <std::shared_ptr <Scene>> (this->managed_scene)) {
        const auto& current = std::get <std::shared_ptr <Scene>> (managed_scene);
        const auto& current_path = current->get_state ().path;
        if (current_path == path) {
            LOG_WARN ("[{}] Scene {} was already loaded.", SCENE_MANAGER_NAME, path.string ());
            return;
        }

        this->cached_scene_states [current_path] = current->get_state ();
        this->notify (SceneEventType::UNLOADED);
        this->managed_scene = std::monostate {};

    }

    const auto factory_func = factory_it->second;
    this->is_loading = true;

    std::thread ([this, path, factory_func, old_state] () mutable {
        LOG_INFO ("[{}] Background loading started: {}", SCENE_MANAGER_NAME, path.string ());

        std::shared_ptr <Scene> scene = factory_func ();
        if (scene && scene->load (path)) {
            if (old_state) {
                scene->set_state (*old_state);
            }

            {
                std::lock_guard lock (this->mutex);
                this->managed_scene = std::move (scene);
                this->is_loading = false;
            }

            this->notify (SceneEventType::LOADED);
        } else {
            LOG_ERROR ("[{}] Failed to load: {}", SCENE_MANAGER_NAME, path.string ());
            {
                std::lock_guard lock (this->mutex);
                this->is_loading = false;
            }
        }
    }).detach ();
}

std::shared_ptr <Scene> SceneManager::get_scene () {
    std::lock_guard lock (this->mutex);
    if (std::holds_alternative <std::shared_ptr <Scene>> (this->managed_scene)) {
        return std::get <std::shared_ptr <Scene>> (this->managed_scene);
    }
    return nullptr;
}

std::optional <std::filesystem::path> SceneManager::get_current_scene_path () const {
    std::lock_guard lock (this->mutex);
    if (std::holds_alternative <std::shared_ptr <Scene>> (this->managed_scene)) {
        return std::get <std::shared_ptr <Scene>> (this->managed_scene)->get_state ().path;
    }
    return std::nullopt;
}

void SceneManager::subscribe (SceneEventCallback callback) {
    this->subscribers.push_back (std::move (callback));
}

void SceneManager::notify (SceneEventType type) {
    for (const auto& callback : this->subscribers) {
        if (callback) {
            callback (type, {});
        }
    }
}

} // sdf_raster

