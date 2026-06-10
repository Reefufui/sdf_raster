// scenes/base/model_manager.hpp
#pragma once

#include "logger.hpp"
#include "scenes/base/model.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace sdf_raster {

#define SCENE_MANAGER_NAME "ModelManager"

enum class ModelEventType {
    LOADED
    , UNLOADED
    , CONFIG_CHANGED
};

using ModelEventCallback = std::function <void (ModelEventType type, const std::filesystem::path& model_path)>;

class ModelManager {
public:
    ModelManager ();
    ~ModelManager ();
    ModelManager (const ModelManager&) = delete;
    ModelManager& operator= (const ModelManager&) = delete;
    ModelManager (ModelManager&&) = delete;
    ModelManager& operator= (ModelManager&&) = delete;

    template <typename ModelType>
    void register_model_type (std::string_view extension) {
        static_assert (std::is_base_of_v <Model, ModelType>, "ModelType must be derived from Model");
        static_assert (std::is_default_constructible_v <ModelType>, "ModelType must be default constructible for the factory");

        std::string ext_str (extension);
        LOG_INFO ("[{}] Registering model type of extension '{}'", SCENE_MANAGER_NAME, ext_str);

        this->factory_registry [ext_str] = [] () {
            return std::make_shared <ModelType> ();
        };
    }

    [[nodiscard]] std::vector <std::string> get_registered_extensions () const;

    std::map <std::filesystem::path, ModelState> get_all_states () const;

    void restore_states (const std::map <std::filesystem::path, ModelState>& states);

    void load_model (const std::filesystem::path& path);

    std::shared_ptr <Model> get_model ();

    [[nodiscard]] std::optional <std::filesystem::path> get_current_model_path () const;

    [[nodiscard]] bool is_loading_scene () const noexcept {
        return is_loading.load (std::memory_order_acquire);
    }

    void wait_for_scene () const;

    void subscribe (ModelEventCallback callback);
    void notify (ModelEventType type);

private:
    void notify (ModelEventType type, const std::filesystem::path& path);

private:
    std::variant <std::monostate, std::shared_ptr <Model>> managed_model;
    std::atomic <bool> is_loading {false};

    std::map <std::filesystem::path, ModelState> cached_model_states;
    std::map <std::string, std::function <std::shared_ptr <Model> ()>> factory_registry;
    std::vector <ModelEventCallback> subscribers;

    mutable std::mutex mutex;
};

} // sdf_raster
