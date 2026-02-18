#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "application.hpp"
#include "compute_shader_renderer.hpp"
#include "cpu_sandbox/cpu_sandbox.h"
#include "marching_cubes.hpp"
#include "mesh_shader_renderer.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

Application::Application (int a_width, int a_height)
    : width ((a_width == 0) ? 1800 : a_width)
    , height ((a_height == 0) ? 900 : a_height)
    , camera ()
    , user_data ({this}) {
    // init_renderer ();
}

Application::Application (int a_width, int a_height, const std::string& a_window_title, size_t a_leaf_memory_limit, bool a_mesh_shader_support)
    : width (a_width)
    , height (a_height)
    , window_title (a_window_title)
    , camera ()
    , user_data ({this}) {
    init_window ();
    init_vulkan (a_mesh_shader_support);
    init_renderer (a_leaf_memory_limit, a_mesh_shader_support);
    last_x = static_cast <float> (width) / 2.0f;
    last_y = static_cast <float> (height) / 2.0f;
}

Application::~Application () {
    this->camera.dump ("/tmp/cached_camera.json");
    glfwSetWindowShouldClose (this->window, true);

    cleanup ();
}

void Application::marching_cubes_cpu (const std::string& a_octree_filename, const std::string& a_mesh_filename) {
    SdfOctree scene {};
    load_sdf_octree (scene, a_octree_filename);

    ///
    MarchingCubesSettings settings;
    settings.iso_level = 0.0f;
    settings.max_threads = 1;
    const std::vector <Mesh> meshes = create_mesh_marching_cubes (settings, scene);
    save_mesh_as_obj (meshes [0], a_mesh_filename); // TODO: mesh concatenation

    ///
    // Payload root_payload;
    // root_payload.node_index = 0;
    // root_payload.voxel_size = 2.f;
    // root_payload.min_corner = {-1.0f, -1.0f, -1.0f};
    // auto subtrees = get_octree_subtrees_payloads (scene, 0);
    // cpu_sandbox::task_generator (subtrees [0], scene.nodes);
    // cpu_sandbox::dump_obj ("result.obj");
}

void Application::run (bool single_frame) {
    if (!this->renderer) {
        throw std::logic_error ("[Application::run] renderer is not inited");
    }

    this->camera.load ("/tmp/cached_camera.json");
    this->last_frame = glfwGetTime ();

    do {
        float current_frame = glfwGetTime ();
        this->delta_time = current_frame - last_frame;
        this->last_frame = current_frame;

        glfwPollEvents ();
        this->process_input ();

        double current_time = glfwGetTime ();
        this->delta_time = static_cast <float> (current_time - last_frame);
        this->last_frame = current_time;

        this->renderer->render (this->camera);
    } while (!glfwWindowShouldClose (this->window) && !single_frame);
}

void Application::init_window () {
    glfwInit ();
    glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint (GLFW_RESIZABLE, GLFW_TRUE);

    this->width = (this->width == 0) ? 1 : this->width;
    this->height = (this->height == 0) ? 1 : this->height;
    this->window = glfwCreateWindow (this->width, this->height, this->window_title.c_str(), nullptr, nullptr);
    if (!this->window) {
        glfwTerminate ();
        throw std::runtime_error ("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer (this->window, &this->user_data);
    glfwSetFramebufferSizeCallback (this->window, framebuffer_resize_callback);
    glfwSetCursorPosCallback (this->window, mouse_callback);
    glfwSetScrollCallback (this->window, scroll_callback);
    glfwSetKeyCallback (this->window, key_callback);
    glfwSetMouseButtonCallback (this->window, mouse_button_callback);

    this->camera_mode_active = false;
    this->first_mouse = true;

    if (this->width == 1 || this->height == 1) {
        glfwMaximizeWindow (this->window);
        glfwGetWindowSize (this->window, &this->width, &this->height);
        std::cout << "Window dimensions: " << this->width << "x" << this->height << std::endl;
    }
}

void Application::init_vulkan (bool a_mesh_shader_support) {
    this->vulkan_context = std::make_shared <VulkanContext> ();
    this->vulkan_context->init (this->window, this->width, this->height, a_mesh_shader_support);
}

void Application::init_renderer (size_t leaf_memory_limit, bool a_mesh_shader_support) {
    if (a_mesh_shader_support) {
        this->renderer = std::make_unique <MeshShaderRenderer> (this->vulkan_context);
    } else {
        this->renderer = std::make_unique <ComputeShaderRenderer> (this->vulkan_context);
    }
    SdfOctree scene {};
    load_sdf_octree (scene, "./assets/sdf/lowpoly_bunny.octree");
    this->renderer->init (this->width, this->height, std::move (scene), leaf_memory_limit);
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
    auto app = get_app_ptr (this->window);

    if (this->camera_mode_active) {
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
    if (!app) return;

    if (app->camera_mode_active) {
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

void Application::key_callback (GLFWwindow* a_window, int key, int scancode, int action, int mods) {
    Application* app = get_app_ptr (a_window);
    if (!app) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (app->camera_mode_active) {
            app->camera_mode_active = false;
            glfwSetInputMode (a_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            std::cout << "Exited camera mode (ESC). Cursor NORMAL." << std::endl;
        }
    }

    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        app->camera.reset ();
        app->first_mouse = true;
    }

    if (key == GLFW_KEY_C) {
        if (action == GLFW_PRESS) {
            app->c_key_pressed_this_frame = true;
        } else if (action == GLFW_RELEASE) {
            if (app->c_key_pressed_this_frame) {
                app->renderer->toggle_frustum_buffer (app->camera);
                std::cout << "Toggled frustum buffer visibility." << std::endl;
                app->c_key_pressed_this_frame = false;
            }
        }
    }
}

void Application::mouse_button_callback (GLFWwindow* a_window, int button, int action, int mods) {
    Application* app = get_app_ptr (a_window);
    if (!app) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (!app->camera_mode_active) {
            app->camera_mode_active = true;
            glfwSetInputMode (a_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            app->first_mouse = true;
            std::cout << "Entered camera mode (Mouse Click). Cursor DISABLED." << std::endl;
        }
    }
}

Application* Application::get_app_ptr (GLFWwindow* a_window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(a_window))->app;
}

}

