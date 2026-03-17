#pragma once

#include "scenes/scene_state.hpp"

#include <chrono>
#include <filesystem>
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

class Scene {
public:
    virtual ~Scene () = default;
    virtual bool load (const std::filesystem::path& path) = 0;
    virtual SceneState get_state () const = 0;
};

class SceneManager {
private:
    struct ManagedScene {
        SceneState state; 

        std::variant <
            std::monostate, 
            std::future <std::unique_ptr <Scene>>, 
            std::unique_ptr <Scene>
        > data;
    };

    std::map <std::filesystem::path, ManagedScene> m_scenes;
    mutable std::mutex m_mutex;

public:
    SceneManager () = default;
    SceneManager (const SceneManager&) = delete;
    SceneManager& operator= (const SceneManager&) = delete;
    SceneManager (SceneManager&&) = delete;
    SceneManager& operator= (SceneManager&&) = delete;

    template <typename SceneType>
    void load_scene (const std::filesystem::path& path);

    Scene* get_scene (const std::filesystem::path& path);

    void unload_scene (const std::filesystem::path& path);

    std::optional <SceneState> get_cached_state (const std::filesystem::path& path) const;
};

} // sdf_raster

