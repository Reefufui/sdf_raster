// application/gui/gui_application.cpp
#include "gui_application.hpp"

#include "imgui_overlay.hpp"

#include "scenes/obj/obj.hpp"
#include "scenes/octree/octree.hpp"
#include "scenes/scomtree/scomtree.hpp"

#include <chrono>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace sdf_raster {

GUIApplication::GUIApplication (SessionState& session)
    : session (session)
    , user_data ({this}) {
    try {
        init_window ();
        init_vulkan ();
        init_renderer ();
        init_model_manager ();
        init_gui ();
    } catch (...) {
        cleanup ();
        throw;
    }
}

GUIApplication::~GUIApplication () {
    this->session.settings.window_maximized = glfwGetWindowAttrib (this->window, GLFW_MAXIMIZED);
    glfwGetWindowSize (window, &this->session.settings.window_width, &this->session.settings.window_height);
    glfwSetWindowShouldClose (this->window, true);

    {
        this->session.settings.frustum_view = false;
        if (this->renderer) {
            this->renderer.reset ();
        }
    }

    this->session.model_states = this->model_manager->get_all_states ();
    this->model_manager.reset ();

    cleanup ();
}

void GUIApplication::run () {
    if (!this->renderer) {
        cleanup ();
        throw std::logic_error ("[GUIApplication::run] renderer is not inited");
    }

    Scene scene;
    // scene.load ("/Users/andreytrifonov/Development/sdf_raster/debug-build/assets/scenes/teapots.json", *this->model_manager);
    // scene.load ("/Users/andreytrifonov/Development/sdf_raster/debug-build/assets/scenes/sphere.json", *this->model_manager);
    scene.load ("/Users/andreytrifonov/Development/sdf_raster/debug-build/assets/scenes/test_scene.json", *this->model_manager);

    try {
        const uint32_t frames_in_flight = this->presentation_render_target->get_max_frames_in_flight ();
        for (uint32_t i = 0; !glfwWindowShouldClose (this->window); i = (i + 1) % frames_in_flight) {
            glfwPollEvents ();

            int iconified = glfwGetWindowAttrib (this->window, GLFW_ICONIFIED);
            if (iconified) {
                std::this_thread::sleep_for (std::chrono::milliseconds (100));
                continue;
            }

            auto cmd_buff = this->presentation_render_target->begin_frame (i);
            if (cmd_buff == VK_NULL_HANDLE) {
                continue;
            }

            gui::update (scene, this->session.settings, this->renderer->get_stats ());

            double current_time = glfwGetTime ();
            float delta_time = static_cast <float> (current_time - this->last_time);
            this->last_time = current_time;

            this->renderer->process_commands (this->render_commands, this->render_command_mutex);
            this->renderer->update (i, this->session.settings, delta_time);

            // this->renderer->render (cmd_buff);
            this->renderer->render_scene (cmd_buff, this->session.settings, scene);
            gui::draw (this->presentation_render_target->get_swapchain_image_index (), cmd_buff);

            this->presentation_render_target->end_frame (cmd_buff, i);
        }
    } catch (...) {
        cleanup ();
        throw;
    }
}

void GUIApplication::init_window () {
    glfwInit ();
    glfwWindowHint (GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint (GLFW_RESIZABLE, GLFW_TRUE);

    this->window = glfwCreateWindow (this->session.settings.window_width, this->session.settings.window_height, APP_NAME, nullptr, nullptr);
    if (this->session.settings.window_maximized) {
        glfwMaximizeWindow (this->window);
    }
    if (!this->window) {
        glfwTerminate ();
        throw std::runtime_error ("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer (this->window, &this->user_data);
    glfwSetFramebufferSizeCallback (this->window, framebuffer_resize_callback);
    glfwSetMouseButtonCallback (this->window, mouse_button_callback);

    this->session.settings.disabled_cursor = false;
}

void GUIApplication::init_vulkan () {
    if (!glfwVulkanSupported ()) {
        throw std::runtime_error ("Vulkan not supported.");
    }
    this->vulkan_context = std::make_shared <VulkanContext> ();
    this->vulkan_context->init ();
}

void GUIApplication::init_renderer () {
    this->presentation_render_target = std::make_shared <PresentationRenderTarget> (this->vulkan_context, this->window);

    auto resize_camera = [&] () {
        const auto extent = this->presentation_render_target->get_extent ();
        const float height = static_cast <float> (extent.height);
        const float width = static_cast <float> (extent.width);
        auto scene = this->model_manager->get_model ();
        if (scene) {
            scene->get_state ().camera.set_aspect_ratio (width / height);
        }
    };

    auto resize_gui = [&] () {
        gui::cleanup (this->session.settings);
        init_gui ();
    };

    auto resize_renderer = [&] () {
        this->renderer->resize ();
    };

    this->presentation_render_target->register_resizable (resize_camera);
    this->presentation_render_target->register_resizable (resize_gui);
    this->presentation_render_target->register_resizable (resize_renderer);
    this->renderer = std::make_unique <Renderer> (this->vulkan_context, this->presentation_render_target);
}

void GUIApplication::init_gui () {
    gui::InitInfo init_info {
        .device = this->vulkan_context->get_device (),
        .window = this->window,
        .instance = this->vulkan_context->get_instance (),
        .physical_device = this->vulkan_context->get_physical_device (),
        .graphics_queue = this->vulkan_context->get_graphics_queue (),
        .graphics_queue_family_index = this->vulkan_context->get_graphics_queue_family_index (),
        .swapchain_image_views = this->presentation_render_target->get_image_views (),
        .surface_extent = this->presentation_render_target->get_extent (),
        .surface_format = this->presentation_render_target->get_image_format (),
    };

    gui::init (vulkan_context, model_manager, init_info, this->session.settings);
}

void GUIApplication::init_model_manager () {
    this->model_manager = std::make_unique <ModelManager> ();
    this->model_manager->register_model_type <ObjModel> (".obj");
    this->model_manager->register_model_type <SComTreeModel> (".scomtree");
    this->model_manager->register_model_type <SComTreeModel> (".bin");
    this->model_manager->register_model_type <SdfOctreeModel> (".octree");

    this->model_manager->subscribe ([this] (ModelEventType type, const std::filesystem::path& path) {
        if (type == ModelEventType::LOADED) {
            this->session.current_model_path = this->model_manager->get_current_model_path ();
        } else if (type == ModelEventType::UNLOADED) {
            this->session.current_model_path = std::nullopt;
        }
        this->on_scene_event (type, path);
    });

    this->model_manager->restore_states (this->session.model_states);

    if (session.current_model_path.has_value ()) {
        this->model_manager->load_model (*this->session.current_model_path);
    }
}

void GUIApplication::cleanup () {
    gui::cleanup (this->session.settings);

    if (this->renderer) {
        this->renderer.reset ();
    }

    if (this->presentation_render_target) {
        this->presentation_render_target.reset ();
    }

    if (this->vulkan_context) {
        this->vulkan_context->shutdown ();
    }
    this->vulkan_context.reset ();

    if (this->window) {
        glfwDestroyWindow (this->window);
    }
    glfwTerminate ();
}

void GUIApplication::framebuffer_resize_callback (GLFWwindow* window, int, int) {
    auto app = get_app_ptr (window);
    app->presentation_render_target->set_resized_flag ();
}

void GUIApplication::mouse_button_callback (GLFWwindow* window, int button, int action, int) {
    GUIApplication* app = get_app_ptr (window);
    if (!app) return;

    if (app->session.settings.disabled_cursor) {
        if ((button == GLFW_MOUSE_BUTTON_RIGHT || button == GLFW_MOUSE_BUTTON_LEFT) && action == GLFW_PRESS) {
            app->session.settings.disabled_cursor = false;
            glfwSetInputMode (window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            LOG_INFO ("Exited camera mode. Cursor NORMAL.");
        }
    } else {
        if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
            app->session.settings.disabled_cursor = true;
            glfwSetInputMode (window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            LOG_INFO ("Entered camera mode. Cursor DISABLED.");
        }
    }
}

void GUIApplication::on_scene_event (ModelEventType type, const std::filesystem::path& /*path*/) {
    auto enqueue_render_config = [this] () {
        std::shared_ptr <Model> scene = this->model_manager->get_model ();
        if (scene) {
            std::function<void()> command = [this, scene] () {
                this->renderer->apply_model_config (scene);
            };
            std::lock_guard lock (this->render_command_mutex);
            this->render_commands.push (std::move (command));
        }
    };

    switch (type) {
        case ModelEventType::LOADED: {
            enqueue_render_config ();

            auto resize_camera = [&] () {
                const auto extent = this->presentation_render_target->get_extent ();
                const float height = static_cast <float> (extent.height);
                const float width = static_cast <float> (extent.width);
                this->model_manager->get_model ()->get_state ().camera.set_aspect_ratio (width / height);
            };

            resize_camera ();

            break;
        }

        case ModelEventType::UNLOADED: {
            break;
        }

        case ModelEventType::CONFIG_CHANGED: {
            enqueue_render_config ();
            break;
        }
    }
}

GUIApplication* GUIApplication::get_app_ptr (GLFWwindow* window) {
    return static_cast<UserData*>(glfwGetWindowUserPointer(window))->app;
}

}
