#include <chrono>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "application.hpp"
#include "sdf_rasterizer.hpp"
#include "gui.hpp"
#include "logger.hpp"
#include "marching_cubes.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

Application::Application ()
    : user_data ({this}) {
    try {
        load_state (this->settings, "/tmp/sdf_raster.json");
        init_window ();
        init_vulkan ();
        init_renderer ();
        init_gui ();
        init_scene_manager ();
    } catch (...) {
        cleanup ();
        throw;
    }
}

Application::~Application () {
    settings.window_maximized = glfwGetWindowAttrib (this->window, GLFW_MAXIMIZED);
    glfwGetWindowSize (window, &this->settings.window_width, &this->settings.window_height);
    glfwSetWindowShouldClose (this->window, true);
    cleanup ();
    dump_state (this->settings, "/tmp/sdf_raster.json");
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

void Application::run (bool /*single_frame*/) {
    if (!this->renderer) {
        cleanup ();
        throw std::logic_error ("[Application::run] renderer is not inited");
    }

    try {
        const uint32_t frames_in_flight = this->vulkan_context->get_total_frames ();
        for (uint32_t i = 0; !glfwWindowShouldClose (this->window); i = (i + 1) % frames_in_flight) {
            glfwPollEvents ();

            int iconified = glfwGetWindowAttrib (this->window, GLFW_ICONIFIED);
            if (iconified) {
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
                continue;
            }

            auto cmd_buff = this->vulkan_context->begin_frame (i);
            if (cmd_buff == VK_NULL_HANDLE) {
                continue;
            }

            gui::update (this->settings, this->renderer->get_stats ());

            if (this->scene.name != settings.scene_state.name) { // TODO: scene manager
                load_sdf_octree (this->scene, settings.scene_state.path);
            }

            this->renderer->update (i, this->scene, this->settings);

            this->renderer->render (cmd_buff);
            gui::draw (this->vulkan_context->get_swapchain_image_index (), cmd_buff);

            this->vulkan_context->end_frame (cmd_buff, i);
        }
    } catch (...) {
        cleanup ();
        throw;
    }
}

void Application::init_window () {
    glfwInit ();
    glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint (GLFW_RESIZABLE, GLFW_TRUE);

    this->window = glfwCreateWindow (this->settings.window_width, this->settings.window_height, APP_NAME, nullptr, nullptr);
    if (this->settings.window_maximized) {
        glfwMaximizeWindow (this->window);
    }
    if (!this->window) {
        glfwTerminate ();
        throw std::runtime_error ("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer (this->window, &this->user_data);
    glfwSetFramebufferSizeCallback (this->window, framebuffer_resize_callback);
    glfwSetMouseButtonCallback (this->window, mouse_button_callback);

    this->settings.disabled_cursor = false;

    int width, height;
    glfwGetWindowSize (this->window, &width, &height);
    LOG_INFO ("Window dimensions: {}x{}", width, height);

    float f_width = static_cast <float> (width);
    float f_height = static_cast <float> (height);

    this->settings.scene_state.camera.set_aspect_ratio (f_width / f_height);
}

void Application::init_vulkan () {
    if (!glfwVulkanSupported ()) {
        throw std::runtime_error ("Vulkan not supported.");
    }
    this->vulkan_context = std::make_shared <VulkanContext> ();

    int width, height;
    glfwGetWindowSize (this->window, &width, &height);
    this->vulkan_context->init (this->window, width, height);

    auto resize_camera = [&] () {
        auto extent = this->vulkan_context->get_swapchain_extent ();
        this->settings.scene_state.camera.set_aspect_ratio (static_cast <float> (extent.width) / static_cast <float> (extent.height));
    };

    auto resize_gui = [&] () {
        gui::cleanup (this->settings);
        init_gui ();
    };

    this->vulkan_context->register_resizable (resize_camera);
    this->vulkan_context->register_resizable (resize_gui);
}

void Application::init_renderer () {
    if (this->settings.use_mesh_shading && !this->vulkan_context->get_use_mesh_shading ()) {
        LOG_WARN ("[Application] Turned off 'use_mesh_shading' settings: device doesn't support mesh shading.");
        this->settings.use_mesh_shading = false;
    }
    this->renderer = std::make_unique <SDFRasterizer> (this->vulkan_context);
    this->renderer->init ();
}

void Application::init_gui () {
    std::vector <VkImageView> swapchain_image_views (this->vulkan_context->get_swapchain_image_count ());
    for (size_t i = 0; i < swapchain_image_views.size (); i++) {
        swapchain_image_views [i] = this->vulkan_context->get_swapchain_image_view (i);
    }

    // TODO: pass vulkan_context to gui. init it the same way as renderer
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

    gui::init (init_info, this->settings);
}

void Application::init_scene_manager () {
    this->scene_manager = std::make_unique <SceneManager> ();
}

void Application::cleanup () {
    gui::cleanup (this->settings);

    if (this->renderer) {
        this->renderer->shutdown (this->settings);
    }

    if (this->vulkan_context) {
        this->vulkan_context->shutdown ();
    }

    if (this->window) {
        glfwDestroyWindow (this->window);
    }
    glfwTerminate ();
}

void Application::framebuffer_resize_callback (GLFWwindow* window, int, int) {
    auto app = get_app_ptr (window);
    app->vulkan_context->set_resized_flag ();
}

void Application::mouse_button_callback (GLFWwindow* window, int button, int action, int) {
    Application* app = get_app_ptr (window);
    if (!app) return;

    if (app->settings.disabled_cursor) {
        if ((button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_LEFT) && action == GLFW_PRESS) {
            app->settings.disabled_cursor = false;
            glfwSetInputMode (window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            LOG_INFO ("Exited camera mode. Cursor NORMAL.");
        }
    } else {
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            app->settings.disabled_cursor = true;
            glfwSetInputMode (window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            LOG_INFO ("Entered camera mode. Cursor DISABLED.");
        }
    }
}

Application* Application::get_app_ptr (GLFWwindow* window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(window))->app;
}

}

