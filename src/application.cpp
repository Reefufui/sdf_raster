#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "application.hpp"
#include "marching_cubes.hpp"
#include "sdf_octree.hpp"
#include "mesh_shader_renderer.hpp"

namespace sdf_raster {

Application::Application (int a_width, int a_height)
    : width (a_width)
    , height (a_height)
    , camera ()
    , user_data ({this}) {
    // init_renderer ();
}

Application::Application(int a_width, int a_height, const std::string& a_window_title)
    : width (a_width)
    , height (a_height)
    , window_title (a_window_title)
    , camera ()
    , user_data ({this}) {
    init_window ();
    init_vulkan ();
    init_renderer ();
    last_x = static_cast <float> (width) / 2.0f;
    last_y = static_cast <float> (height) / 2.0f;
}

Application::~Application() {
    cleanup();
}

void Application::marching_cubes_cpu (const std::string& a_octree_filename, const std::string& a_mesh_filename) {
    SdfOctree scene {};
    load_sdf_octree (scene, a_octree_filename);
    MarchingCubesSettings settings;
    settings.iso_level = 0.0f;
    settings.max_threads = 1;
    const std::vector <Mesh> meshes = create_mesh_marching_cubes (settings, scene);
    save_mesh_as_obj (meshes [0], a_mesh_filename); // TODO: mesh concatenation
}

void Application::run () {
    if (!this->renderer) {
        throw std::logic_error ("[Application::run] renderer is not inited");
    }

    this->last_frame = glfwGetTime ();

    while (!glfwWindowShouldClose (this->window)) {
        float current_frame = glfwGetTime ();
        this->delta_time = current_frame - last_frame;
        this->last_frame = current_frame;

        glfwPollEvents ();
        this->process_input ();

        double current_time = glfwGetTime ();
        this->delta_time = static_cast <float> (current_time - last_frame);
        this->last_frame = current_time;

        this->renderer->render (this->camera);
    }
}

void Application::init_window () {
    glfwInit ();
    glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint (GLFW_RESIZABLE, GLFW_TRUE);

    this->window = glfwCreateWindow (this->width, this->height, this->window_title.c_str(), nullptr, nullptr);
    if (!this->window) {
        glfwTerminate ();
        throw std::runtime_error ("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer (this->window, &this->user_data);
    glfwSetFramebufferSizeCallback (this->window, framebuffer_resize_callback);
    glfwSetCursorPosCallback (this->window, mouse_callback);
    glfwSetScrollCallback (this->window, scroll_callback);

    glfwSetInputMode (this->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Application::init_vulkan () {
    this->vulkan_context = std::make_shared <VulkanContext> ();
    this->vulkan_context->init (this->window, this->width, this->height);
}

void Application::init_renderer () {
    this->renderer = std::make_unique <MeshShaderRenderer> (this->vulkan_context);
    SdfOctree scene {};
    load_sdf_octree (scene, "./assets/sdf/example_octree_large.octree");
    this->renderer->init (this->width, this->height, std::move (scene));
}

void Application::cleanup () {
    if (this->renderer) {
        this->renderer->shutdown ();
    }

    if (this->vulkan_context) {
        this->vulkan_context->shutdown ();
    }

    if (this->window) {
        glfwDestroyWindow (this->window);
    }
    glfwTerminate ();
}

void Application::process_input () {
    if (glfwGetKey (this->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose (this->window, true);

    if (glfwGetKey (this->window, GLFW_KEY_W) == GLFW_PRESS)
        this->camera.process_keyboard_input (FORWARD, this->delta_time);
    if (glfwGetKey (this->window, GLFW_KEY_S) == GLFW_PRESS)
        this->camera.process_keyboard_input (BACKWARD, this->delta_time);
    if (glfwGetKey (this->window, GLFW_KEY_A) == GLFW_PRESS)
        this->camera.process_keyboard_input (LEFT, this->delta_time);
    if (glfwGetKey (this->window, GLFW_KEY_D) == GLFW_PRESS)
        this->camera.process_keyboard_input (RIGHT, this->delta_time);
    if (glfwGetKey (this->window, GLFW_KEY_SPACE) == GLFW_PRESS)
        this->camera.process_keyboard_input (UP, this->delta_time);
    if (glfwGetKey (this->window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        this->camera.process_keyboard_input (DOWN, this->delta_time);
}

void Application::framebuffer_resize_callback (GLFWwindow* a_window, int a_width, int a_height) {
    auto app = get_app_ptr (a_window);
    if (app && app->renderer) {
        app->width = a_width;
        app->height = a_height;
        app->renderer->resize (a_width, a_height);
    }
}

void Application::mouse_callback (GLFWwindow* a_window, double xpos, double ypos) {
    auto app = get_app_ptr (a_window);
    if (app) {
        if (app->first_mouse) {
            app->last_x = static_cast <float> (xpos);
            app->last_y = static_cast <float> (ypos);
            app->first_mouse = false;
        }

        float xoffset = static_cast <float> (xpos) - app->last_x;
        float yoffset = app->last_y - static_cast <float> (ypos);
        app->last_x = static_cast <float> (xpos);
        app->last_y = static_cast <float> (ypos);

        app->camera.process_mouse_movement (xoffset, yoffset);
    }
}

void Application::scroll_callback (GLFWwindow* a_window, double, double yoffset) {
    auto app = get_app_ptr (a_window);
    if (app) {
        app->camera.fov_y -= static_cast <float> (yoffset);
        if (app->camera.fov_y < 1.0f) app->camera.fov_y = 1.0f;
        if (app->camera.fov_y > 45.0f) app->camera.fov_y = 45.0f;
    }
}

Application* Application::get_app_ptr (GLFWwindow* a_window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(a_window))->app;
}

}

