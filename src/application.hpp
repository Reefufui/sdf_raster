#pragma once

#include <memory>
#include <string>
#include <vector>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "renderer.hpp"
#include "sdf_grid.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class Application {
public:
    Application(int a_width, int a_height);
    Application(int width, int height, const std::string& title);
    ~Application();

    void run();
    void marching_cubes_cpu (const std::string& a_octree_filename, const std::string& a_mesh_filename);

private:
    void cleanup();
    void init_renderer();
    void init_vulkan();
    void init_window();

    static Application* get_app_ptr(GLFWwindow* window);
    static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string window_title;
    Camera camera;

    struct UserData {
        Application* app;
    } user_data;

    std::shared_ptr <VulkanContext> vulkan_context;
    std::unique_ptr <IRenderer> renderer;
};

}

