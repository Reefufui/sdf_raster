#pragma once

#include "logger.hpp"
#include "scenes/scene.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
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

    void load_scene (const std::filesystem::path& path);

    void set_current_scene (const std::filesystem::path& path);

    Scene* get_scene (const std::filesystem::path& path);

    Scene* get_current_scene ();

    void unload_scene (const std::filesystem::path& path);

    std::optional <SceneState> get_state (const std::filesystem::path& path) const;

    std::optional <SceneState> get_current_state () const;

    void subscribe (SceneEventCallback callback);

private:
    void notify (SceneEventType type, const std::filesystem::path& path);

    void worker_thread_loop ();

private:
    struct ManagedScene {
        SceneState state;

        std::variant <
            std::monostate
            , std::future <std::unique_ptr <Scene>>
            , std::unique_ptr <Scene>
            > data;
    };

    std::map <std::filesystem::path, ManagedScene> scenes;
    std::optional <std::filesystem::path> current_scene_path;
    std::map <std::string, std::function <std::unique_ptr <Scene> ()>> factory_registry;
    std::vector <SceneEventCallback> subscribers;
    mutable std::mutex mutex;

    std::thread worker;
    std::atomic <bool> is_running {false};
};

} // sdf_raster

