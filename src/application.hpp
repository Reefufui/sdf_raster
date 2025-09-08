#pragma once

#include <memory>
#include <string>
#include <vector>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "cpu_renderer.hpp"
#include "mesh.hpp"
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
    void run(const std::string& a_filename);

private:
    void cleanup();
    void init_renderer();
    void init_vulkan();
    void init_window();
    void main_loop();

    static Application* get_app_ptr(GLFWwindow* window);
    static void cursor_pos_callback(GLFWwindow* window, double xpos, double ypos);
    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string window_title;

    struct UserData {
        Application* app;
    } user_data;

    Camera camera;
    Mesh triangle_mesh;
    std::shared_ptr<VulkanContext> vulkan_context;
    std::unique_ptr<IRenderer> renderer;
};

}

