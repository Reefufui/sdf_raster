#pragma once

#include <memory>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "camera.hpp"
#include "renderer.hpp"
#include "sdf_octree.hpp"
#include "state.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class Application {
public:
    Application ();
    ~Application ();

    void run (bool single_frame);
    void marching_cubes_cpu (const std::string& a_octree_filename, const std::string& a_mesh_filename);

private:
    void cleanup ();
    void init_renderer ();
    void init_vulkan ();
    void init_window ();
    void init_gui ();

    static Application* get_app_ptr (GLFWwindow* window);
    static void framebuffer_resize_callback (GLFWwindow* window, int width, int height);
    static void mouse_button_callback (GLFWwindow* a_window, int button, int action, int mods);

private:
    GLFWwindow* window;

    Settings settings;
    SdfOctree scene;

    struct UserData {
        Application* app;
    } user_data;

    std::shared_ptr <VulkanContext> vulkan_context;
    std::unique_ptr <Renderer> renderer;
};

}

