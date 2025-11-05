#pragma once

#include <memory>
#include <string>
#include <vector>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "mesh_shader_renderer.hpp"
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
    void cleanup ();
    void init_renderer ();
    void init_vulkan ();
    void init_window ();
    void process_input ();

    static Application* get_app_ptr (GLFWwindow* window);
    static void mouse_callback (GLFWwindow* window, double xpos, double ypos);
    static void framebuffer_resize_callback (GLFWwindow* window, int width, int height);
    static void scroll_callback (GLFWwindow* a_window, double xoffset, double yoffset);

private:
    GLFWwindow* window;
    int width;
    int height;
    std::string window_title;

    Camera camera;
    float last_x = 0.0f;
    float last_y = 0.0f;
    bool first_mouse = true;
    float delta_time = 0.0f;
    float last_frame = 0.0f;

    struct UserData {
        Application* app;
    } user_data;

    std::shared_ptr <VulkanContext> vulkan_context;
    std::unique_ptr <MeshShaderRenderer> renderer;
};

}

