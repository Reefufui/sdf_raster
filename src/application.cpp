#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "application.hpp"
#include "marching_cubes.hpp"
#include "sdf_octree.hpp"
#include "mesh_shader_renderer.hpp"

namespace sdf_raster {

Application::Application(int a_width, int a_height)
    : width(a_width)
    , height(a_height)
    , camera({0.0f, 0.0f, 100.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 60.0f, (float)a_width / a_height, 0.1f, 100.0f)
    , user_data({this}) {
    init_renderer ();
}

Application::Application(int a_width, int a_height, const std::string& a_window_title)
    : width (a_width)
    , height (a_height)
    , window_title (a_window_title)
    , camera ({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 45.0f, (float)a_width / a_height, 0.1f, 100.0f)
    , user_data ({this}) {
    init_window ();
    init_vulkan ();
    init_renderer ();
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
    double last_frame_time = glfwGetTime ();
    if (!this->renderer) {
        throw std::logic_error ("[Application::run] renderer is not inited");
    }

    while (!glfwWindowShouldClose (this->window)) {
        glfwPollEvents ();

        double current_time = glfwGetTime ();
        float delta_time = static_cast <float> (current_time - last_frame_time);
        last_frame_time = current_time;

        this->camera.update (this->window, delta_time);
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
    glfwSetCursorPosCallback (this->window, cursor_pos_callback);

    // glfwSetInputMode(this->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

void Application::init_vulkan () {
    this->vulkan_context = std::make_shared <VulkanContext> ();
    this->vulkan_context->init (this->window, this->width, this->height);
}

void Application::init_renderer () {
    this->renderer = std::make_unique <MeshShaderRenderer> (this->vulkan_context);
    this->renderer->init (this->width, this->height);
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

void Application::framebuffer_resize_callback (GLFWwindow* a_window, int a_width, int a_height) {
    auto app = get_app_ptr (a_window);
    if (app && app->renderer) {
        app->width = a_width;
        app->height = a_height;
        app->camera.set_aspect_ratio (static_cast <float> (a_width) / a_height);
        app->renderer->resize (a_width, a_height);
    }
}

void Application::cursor_pos_callback (GLFWwindow*, double, double) {
}

Application* Application::get_app_ptr (GLFWwindow* a_window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(a_window))->app;
}

}

