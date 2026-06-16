// scenes/base/model_manager.cpp
#include "scenes/base/model_manager.hpp"

namespace sdf_raster {

ModelManager::ModelManager () {
}

ModelManager::~ModelManager () {
}

[[nodiscard]] std::vector <std::string> ModelManager::get_registered_extensions () const {
    std::lock_guard lock (this->mutex);

    std::vector <std::string> extensions;
    extensions.reserve (this->factory_registry.size ());
    for (const auto& [extension, factory_func] : this->factory_registry) {
        extensions.push_back (extension);
    }

    return extensions;
}

std::map <std::filesystem::path, ModelState> ModelManager::get_all_states () const {
    std::lock_guard lock (this->mutex);
    std::map <std::filesystem::path, ModelState> result = this->cached_model_states;
    if (std::holds_alternative <std::shared_ptr <Model>> (this->managed_model)) {
        auto& state = std::get <std::shared_ptr <Model>> (this->managed_model)->get_state ();
        result [state.path] = state;
    }
    return result;
}

void ModelManager::restore_states (const std::map <std::filesystem::path, ModelState>& states) {
    std::lock_guard lock (this->mutex);

    for (const auto& [path, state] : states) {
        this->cached_model_states [path] = state;
    }

    LOG_INFO ("[{}] Restored {} model states.", SCENE_MANAGER_NAME, this->cached_model_states.size ());
}

void ModelManager::load_model (const std::filesystem::path& path) {
    std::lock_guard lock (this->mutex);

    if (this->is_loading) {
        LOG_WARN ("[{}] Some model is currently loading. Cannot load {}.", SCENE_MANAGER_NAME, path.string ());
        return;
    }

    const std::string extension = path.extension ().string ();
    auto factory_it = this->factory_registry.find (extension);
    if (factory_it == this->factory_registry.end ()) {
        LOG_ERROR ("[{}] No model type registered for extension '{}'. Cannot load {}.", SCENE_MANAGER_NAME, extension, path.string ());
        return;
    }

    std::optional <ModelState> old_state;
    if (auto cache_it = this->cached_model_states.find (path); cache_it != this->cached_model_states.end ()) {
        old_state = cache_it->second;
    }

    if (std::holds_alternative <std::shared_ptr <Model>> (this->managed_model)) {
        const auto& current = std::get <std::shared_ptr <Model>> (managed_model);
        const auto& current_path = current->get_state ().path;
        if (current_path == path) {
            LOG_WARN ("[{}] Model {} was already loaded.", SCENE_MANAGER_NAME, path.string ());
            return;
        }

        this->cached_model_states [current_path] = current->get_state ();
        this->notify (ModelEventType::UNLOADED);
        this->managed_model = std::monostate {};

    }

    const auto factory_func = factory_it->second;
    this->is_loading = true;

    std::thread ([this, path, factory_func, old_state] () mutable {
        LOG_INFO ("[{}] Background loading started: {}", SCENE_MANAGER_NAME, path.string ());

        std::shared_ptr <Model> model = factory_func ();
        if (model && model->load (path)) {
            if (old_state) {
                model->set_state (*old_state);
            }

            {
                std::lock_guard lock (this->mutex);
                this->managed_model = std::move (model);
                this->is_loading = false;
            }

            this->notify (ModelEventType::LOADED);
        } else {
            LOG_ERROR ("[{}] Failed to load: {}", SCENE_MANAGER_NAME, path.string ());
            {
                std::lock_guard lock (this->mutex);
                this->is_loading = false;
            }
        }
    }).detach ();
}

void ModelManager::wait_for_scene () const {
    while (this->is_loading.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }
}

std::shared_ptr <Model> ModelManager::create_model (const std::filesystem::path& path) {
    std::lock_guard lock (this->mutex);
    if (auto it = this->model_cache.find (path); it != this->model_cache.end ()) {
        if (auto shared = it->second.lock ()) {
            LOG_INFO ("[{}] Model {} found in cache.", SCENE_MANAGER_NAME, path.string ());
            return shared;
        }
        this->model_cache.erase (it);
    }
    const std::string extension = path.extension ().string ();
    auto factory_it = this->factory_registry.find (extension);
    if (factory_it == this->factory_registry.end ()) {
        LOG_ERROR ("[{}] No model type registered for extension '{}'.", SCENE_MANAGER_NAME, extension);
        return nullptr;
    }
    std::shared_ptr <Model> model = factory_it->second ();
    if (model && model->load (path)) {
        this->model_cache [path] = model;
        return model;
    }
    return nullptr;
}

std::shared_ptr <Model> ModelManager::get_model () {
    std::lock_guard lock (this->mutex);
    if (std::holds_alternative <std::shared_ptr <Model>> (this->managed_model)) {
        return std::get <std::shared_ptr <Model>> (this->managed_model);
    }
    return nullptr;
}

std::optional <std::filesystem::path> ModelManager::get_current_model_path () const {
    std::lock_guard lock (this->mutex);
    if (std::holds_alternative <std::shared_ptr <Model>> (this->managed_model)) {
        return std::get <std::shared_ptr <Model>> (this->managed_model)->get_state ().path;
    }
    return std::nullopt;
}

void ModelManager::subscribe (ModelEventCallback callback) {
    this->subscribers.push_back (std::move (callback));
}

void ModelManager::notify (ModelEventType type) {
    for (const auto& callback : this->subscribers) {
        if (callback) {
            callback (type, {});
        }
    }
}

} // sdf_raster
