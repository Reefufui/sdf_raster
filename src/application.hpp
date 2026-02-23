#pragma once

#include <memory>
#include <string>
#include <vector>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "renderer.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class Application {
public:
    Application (int a_width, int a_height);
    Application (int width, int height, bool a_mesh_shader_support);
    ~Application ();

    void run (bool single_frame);
    void marching_cubes_cpu (const std::string& a_octree_filename, const std::string& a_mesh_filename);

private:
    void cleanup ();
    void init_renderer (bool a_mesh_shader_suppport);
    void init_vulkan (bool a_mesh_shader_suppport);
    void init_window ();
    void init_gui ();
    void process_input ();

    static Application* get_app_ptr (GLFWwindow* window);
    static void mouse_callback (GLFWwindow* window, double xpos, double ypos);
    static void framebuffer_resize_callback (GLFWwindow* window, int width, int height);
    static void scroll_callback (GLFWwindow* a_window, double xoffset, double yoffset);
    static void key_callback (GLFWwindow* a_window, int key, int scancode, int action, int mods);
    static void mouse_button_callback (GLFWwindow* a_window, int button, int action, int mods);

private:
    GLFWwindow* window;
    int width;
    int height;

    bool camera_mode_active = true;
    bool escape_pressed_last_frame = false;
    bool c_key_pressed_this_frame = false;

    Camera camera;
    float last_x = 0.0f;
    float last_y = 0.0f;
    bool first_mouse = true;
    float delta_time = 0.0f;
    float last_frame = 0.0f;

    std::vector <float> frame_times;
    const size_t max_frame_times = 60;
    float total_frame_time = 0.0f;
    bool dump_snapshot = false;

    struct UserData {
        Application* app;
    } user_data;

    std::shared_ptr <VulkanContext> vulkan_context;
    std::unique_ptr <Renderer> renderer;
};

}

