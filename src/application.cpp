#include <chrono>
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "application.hpp"
#include "gui.hpp"
#include "logger.hpp"
#include "scenes/obj/obj.hpp"
#include "scenes/octree/octree.hpp"
#include "scenes/scomtree/scomtree.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

Application::Application ()
    : user_data ({this}) {
    try {
        SessionState session;
        load_session (session, "/tmp/sdf_raster.json");
        this->settings = session.settings;

        init_window ();
        init_vulkan ();
        init_renderer ();
        init_scene_manager (session);
        init_gui ();
    } catch (...) {
        cleanup ();
        throw;
    }
}

Application::~Application () {
    this->settings.window_maximized = glfwGetWindowAttrib (this->window, GLFW_MAXIMIZED);
    glfwGetWindowSize (window, &this->settings.window_width, &this->settings.window_height);
    glfwSetWindowShouldClose (this->window, true);

    {
        // NOTE: We should destroy renderer prior to saving scene states
        // If we want it to return camera captured during frustum demo.
        this->settings.frustum_view = false;
        if (this->renderer) {
            this->renderer->shutdown ();
        }
    }

    SessionState session_to_save;
    session_to_save.settings = this->settings;
    session_to_save.scene_states = this->scene_manager->get_all_states ();
    if (auto scene = this->scene_manager->get_scene ()) {
        session_to_save.current_scene_path = scene->get_state ().path;
    }

    dump_session (session_to_save, "/tmp/sdf_raster.json");
    this->scene_manager.reset ();

    cleanup ();
}

void Application::run () {
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

            this->renderer->process_commands (this->render_commands, this->render_command_mutex);
            this->renderer->update (i, this->settings); // TODO: update everything through 'process_commands'

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
        const auto extent = this->vulkan_context->get_swapchain_extent ();
        const float height = static_cast <float> (extent.height);
        const float width = static_cast <float> (extent.width);
        auto scene = this->scene_manager->get_scene ();
        if (scene) {
            scene->get_state ().camera.set_aspect_ratio (width / height);
        }
    };

    auto resize_gui = [&] () {
        gui::cleanup (this->settings);
        init_gui ();
    };

    this->vulkan_context->register_resizable (resize_camera);
    this->vulkan_context->register_resizable (resize_gui);
}

void Application::init_renderer () {
    this->renderer = std::make_unique <SDFRasterizer> (this->vulkan_context);
    this->renderer->init ();
}

void Application::init_gui () {
    // TODO: pass vulkan_context to gui. init it the same way as renderer
    gui::InitInfo init_info {
        .device = this->vulkan_context->get_device (),
        .window = this->window,
        .instance = this->vulkan_context->get_instance (),
        .physical_device = this->vulkan_context->get_physical_device (),
        .graphics_queue = this->vulkan_context->get_graphics_queue (),
        .graphics_queue_family_index = this->vulkan_context->get_graphics_queue_family_index (),
        .swapchain_image_views = this->vulkan_context->get_swapchain_image_views (),
        .surface_extent = this->vulkan_context->get_swapchain_extent (),
        .surface_format = this->vulkan_context->get_swapchain_image_format (),
        .depth_format = this->vulkan_context->get_depth_format (),
    };

    gui::init (vulkan_context, scene_manager, init_info, this->settings);
}

void Application::init_scene_manager (const SessionState& session) {
    this->scene_manager = std::make_unique <SceneManager> ();
    this->scene_manager->register_scene_type <ObjScene> (".obj");
    this->scene_manager->register_scene_type <SComTreeScene> (".scomtree");
    this->scene_manager->register_scene_type <SdfOctreeScene> (".octree");

    this->scene_manager->subscribe ([this] (SceneEventType type, const std::filesystem::path& path) {
        this->on_scene_event (type, path);
    });

    this->scene_manager->restore_states (session.scene_states);

    if (session.current_scene_path.has_value ()) {
        this->scene_manager->load_scene (*session.current_scene_path);
    }
}

void Application::cleanup () {
    gui::cleanup (this->settings);

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

void Application::on_scene_event (SceneEventType type, const std::filesystem::path& /*path*/) {
    auto enqueue_render_config = [this] () {
        std::shared_ptr <Scene> scene = this->scene_manager->get_scene ();
        if (scene) {
            RenderCommand command = [scene] (Renderer* renderer) {
                if (SDFRasterizer* sdf_rasterizer = dynamic_cast <SDFRasterizer*> (renderer)) {
                    sdf_rasterizer->apply_scene_config (scene);
                }
            };
            std::lock_guard lock (this->render_command_mutex);
            this->render_commands.push (std::move (command));
        }
    };

    switch (type) {
        case SceneEventType::LOADED: {
            enqueue_render_config ();

            auto resize_camera = [&] () {
                const auto extent = this->vulkan_context->get_swapchain_extent ();
                const float height = static_cast <float> (extent.height);
                const float width = static_cast <float> (extent.width);
                this->scene_manager->get_scene ()->get_state ().camera.set_aspect_ratio (width / height);
            };

            resize_camera (); // TODO: class method

            break;
        }

        case SceneEventType::UNLOADED: {
            break;
        }

        case SceneEventType::CONFIG_CHANGED: {
            enqueue_render_config ();
            break;
        }
    }
}

Application* Application::get_app_ptr (GLFWwindow* window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(window))->app;
}

}

