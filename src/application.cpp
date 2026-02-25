#include <chrono>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "application.hpp"
#include "compute_shader_renderer.hpp"
#include "cpu_sandbox/cpu_sandbox.h"
#include "gui.hpp"
#include "logger.hpp"
#include "marching_cubes.hpp"
#include "mesh_shader_renderer.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

Application::Application ()
    : user_data ({this}) {
    try {
        init_window ();
        init_vulkan ();
        init_renderer ();
        // init_gui ();
    } catch (...) {
        cleanup ();
        throw;
    }
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
        cleanup ();
        throw std::logic_error ("[Application::run] renderer is not inited");
    }

    try {
        this->camera.load ("/tmp/cached_camera.json");
        this->last_frame = glfwGetTime ();

        do {
            float current_frame = glfwGetTime ();
            this->delta_time = current_frame - last_frame;
            this->last_frame = current_frame;

            this->frame_times.push_back (this->delta_time);
            total_frame_time += this->delta_time;

            if (frame_times.size () > max_frame_times) {
                total_frame_time -= frame_times.front ();
                frame_times.erase (frame_times.begin ());
            }

            glfwPollEvents ();
            int iconified = glfwGetWindowAttrib (this->window, GLFW_ICONIFIED);
            if (iconified) {
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
                continue;
            }

            this->process_input ();

            double current_time = glfwGetTime ();
            this->delta_time = static_cast <float> (current_time - last_frame);
            this->last_frame = current_time;

            this->camera.update ();
            this->renderer->render (this->camera);

            if (this->dump_snapshot) {
                float current_fps = (total_frame_time > 0.0f && !frame_times.empty()) ? (static_cast<float>(frame_times.size()) / total_frame_time) : 0.0f;
                LOG_INFO ("FPS: {:.2f}", current_fps);
                this->dump_snapshot = false;
            }
        } while (!glfwWindowShouldClose (this->window) && !single_frame);
    } catch (...) {
        cleanup ();
        throw;
    }
}

void Application::init_window () {
    glfwInit ();
    glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint (GLFW_RESIZABLE, GLFW_TRUE);

    glfwWindowHint (GLFW_MAXIMIZED, GLFW_TRUE);
    this->window = glfwCreateWindow (1080, 900, APP_NAME, nullptr, nullptr);
    if (!this->window) {
        glfwTerminate ();
        throw std::runtime_error ("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer (this->window, &this->user_data);
    glfwSetCursorPosCallback (this->window, mouse_callback);
    glfwSetScrollCallback (this->window, scroll_callback);
    glfwSetKeyCallback (this->window, key_callback);
    glfwSetMouseButtonCallback (this->window, mouse_button_callback);

    glfwSetInputMode (this->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    this->camera_mode_active = true;
    this->first_mouse = true;

    int width, height;
    glfwGetWindowSize (this->window, &width, &height);
    LOG_INFO ("Window dimensions: {}x{}", width, height);

    float f_width = static_cast <float> (width);
    float f_height = static_cast <float> (height);

    this->camera = Camera ();
    this->camera.set_aspect_ratio (f_width / f_height);

    this->last_x = f_width / 2.0f;
    this->last_y = f_height / 2.0f;
}

void Application::init_vulkan () {
    if (!glfwVulkanSupported ()) {
        throw std::runtime_error ("Vulkan not supported.");
    }
    this->vulkan_context = std::make_shared <VulkanContext> ();

    int width, height;
    glfwGetWindowSize (this->window, &width, &height);
    this->vulkan_context->init (this->window, width, height, false);

    auto resize_camera = [&] () {
        auto extent = this->vulkan_context->get_swapchain_extent ();
        this->camera.set_aspect_ratio (static_cast <float> (extent.width) / static_cast <float> (extent.height));
    };
    this->vulkan_context->register_resizable (resize_camera);
}

void Application::init_renderer () {
    this->renderer = std::make_unique <ComputeShaderRenderer> (this->vulkan_context);

    SdfOctree scene {};
    load_sdf_octree (scene, "./assets/sdf/lowpoly_bunny.octree");
    this->renderer->init (std::move (scene));
}

void Application::init_gui () {
    std::vector <VkImageView> swapchain_image_views (this->vulkan_context->get_swapchain_image_count ());
    for (size_t i = 0; i < swapchain_image_views.size (); i++) {
        swapchain_image_views [i] = this->vulkan_context->get_swapchain_image_view (i);
    }

    gui::InitInfo init_info {
        .device = this->vulkan_context->get_device (),
        .window = this->window,
        .instance = this->vulkan_context->get_instance (),
        .physical_device = this->vulkan_context->get_physical_device (),
        .graphics_queue = this->vulkan_context->get_graphics_queue (),
        .graphics_queue_family_index = this->vulkan_context->get_graphics_queue_family_index (),
        .swapchain_image_views = swapchain_image_views,
        .surface_extent = this->vulkan_context->get_swapchain_extent (),
        .surface_format = this->vulkan_context->get_swapchain_image_format (),
        .depth_format = this->vulkan_context->get_depth_format (),
    };

    gui::init (init_info);
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
    if (this->camera_mode_active) {
        if (glfwGetKey (this->window, GLFW_KEY_W) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::FORWARD, this->delta_time);
        if (glfwGetKey (this->window, GLFW_KEY_S) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::BACKWARD, this->delta_time);
        if (glfwGetKey (this->window, GLFW_KEY_A) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::LEFT, this->delta_time);
        if (glfwGetKey (this->window, GLFW_KEY_D) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::RIGHT, this->delta_time);
        if (glfwGetKey (this->window, GLFW_KEY_SPACE) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::UP, this->delta_time);
        if (glfwGetKey (this->window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            this->camera.process_keyboard_input (Camera::Movement::DOWN, this->delta_time);
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
        app->camera.process_scroll (static_cast <float> (yoffset));
    }
}

void Application::key_callback (GLFWwindow* a_window, int key, int, int action, int) {
    Application* app = get_app_ptr (a_window);
    if (!app) return;

    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (app->camera_mode_active) {
            app->camera_mode_active = false;
            glfwSetInputMode (a_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            LOG_INFO ("Exited camera mode. Cursor NORMAL.");
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
                LOG_INFO ("Toggled frustum buffer visibility.");
                app->c_key_pressed_this_frame = false;
            }
        }
    }

    if (key == GLFW_KEY_I && action == GLFW_PRESS) {
        app->dump_snapshot = true;
    }
}

void Application::mouse_button_callback (GLFWwindow* a_window, int button, int action, int) {
    Application* app = get_app_ptr (a_window);
    if (!app) return;

    if (app->camera_mode_active) {
        if ((button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_LEFT) && action == GLFW_PRESS) {
            app->camera_mode_active = false;
            glfwSetInputMode (a_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            LOG_INFO ("Exited camera mode. Cursor NORMAL.");
        }
    } else {
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            app->camera_mode_active = true;
            glfwSetInputMode (a_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            app->first_mouse = true;
            LOG_INFO ("Entered camera mode. Cursor DISABLED.");
        }
    }
}

Application* Application::get_app_ptr (GLFWwindow* a_window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(a_window))->app;
}

}

