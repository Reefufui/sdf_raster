// application/gui/gui_application.hpp
#pragma once

#include "presentation_render_target.hpp"

#include "engine/camera.hpp"
#include "engine/sdf_rasterizer.hpp"
#include "scenes/base/scene_manager.hpp"
#include "scenes/octree/octree.hpp"
#include "state.hpp"
#include "vulkan/vulkan_context.hpp"

#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sdf_raster {

class GUIApplication {
public:
    GUIApplication ();
    ~GUIApplication ();

    void run ();

private:
    void cleanup ();
    void init_renderer ();
    void init_vulkan ();
    void init_window ();
    void init_gui ();
    void init_scene_manager (const SessionState& session);

    static GUIApplication* get_app_ptr (GLFWwindow* window);
    static void framebuffer_resize_callback (GLFWwindow* window, int width, int height);
    static void mouse_button_callback (GLFWwindow* a_window, int button, int action, int mods);

    void on_scene_event (SceneEventType type, const std::filesystem::path& path);

private:
    GLFWwindow* window;

    Settings settings;
    SdfOctree scene;

    struct UserData {
        GUIApplication* app;
    } user_data;

    std::shared_ptr <VulkanContext> vulkan_context;
    std::shared_ptr <PresentationRenderTarget> presentation_render_target;
    std::shared_ptr <SceneManager> scene_manager;

    std::unique_ptr <SDFRasterizer> renderer;
    std::mutex render_command_mutex;
    std::queue <std::function<void()>> render_commands;
};

}
