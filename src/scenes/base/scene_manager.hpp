// scenes/base/scene_manager.hpp
#pragma once

#include "logger.hpp"
#include "scenes/base/scene.hpp"

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

#define SCENE_MANAGER_NAME "SceneManager"

enum class SceneEventType {
    LOADED
    , UNLOADED
    , CONFIG_CHANGED
};

using SceneEventCallback = std::function <void (SceneEventType type, const std::filesystem::path& scene_path)>;

class SceneManager {
public:
    SceneManager ();
    ~SceneManager ();
    SceneManager (const SceneManager&) = delete;
    SceneManager& operator= (const SceneManager&) = delete;
    SceneManager (SceneManager&&) = delete;
    SceneManager& operator= (SceneManager&&) = delete;

    template <typename SceneType>
    void register_scene_type (std::string_view extension) {
        static_assert (std::is_base_of_v <Scene, SceneType>, "SceneType must be derived from Scene");
        static_assert (std::is_default_constructible_v <SceneType>, "SceneType must be default constructible for the factory");

        std::string ext_str (extension);
        LOG_INFO ("[{}] Registering scene type of extension '{}'", SCENE_MANAGER_NAME, ext_str);

        this->factory_registry [ext_str] = [] () {
            return std::make_unique <SceneType> ();
        };
    }

    [[nodiscard]] std::vector <std::string> get_registered_extensions () const;

    std::map <std::filesystem::path, SceneState> get_all_states () const;

    void restore_states (const std::map <std::filesystem::path, SceneState>& states);

    void load_scene (const std::filesystem::path& path);

    std::shared_ptr <Scene> get_scene ();

    [[nodiscard]] std::optional <std::filesystem::path> get_current_scene_path () const;

    [[nodiscard]] bool is_loading_scene () const noexcept {
        return is_loading.load (std::memory_order_acquire);
    }

    void wait_for_scene () const;

    void subscribe (SceneEventCallback callback);
    void notify (SceneEventType type);

private:
    void notify (SceneEventType type, const std::filesystem::path& path);

private:
    std::variant <std::monostate, std::shared_ptr <Scene>> managed_scene;
    std::atomic <bool> is_loading {false};

    std::map <std::filesystem::path, SceneState> cached_scene_states;
    std::map <std::string, std::function <std::shared_ptr <Scene> ()>> factory_registry;
    std::vector <SceneEventCallback> subscribers;

    mutable std::mutex mutex;
};

} // sdf_raster

