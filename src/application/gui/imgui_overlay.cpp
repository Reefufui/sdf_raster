// application/gui/imgui_overlay.cpp
#include <algorithm>
#include <cassert>
#include <chrono>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "imfilebrowser.h"

#include "gui_application.hpp"
#include "imgui_overlay.hpp"
#include "../../scenes/base/scene_manager.hpp"
#include "../../scenes/scene_state.hpp"
#include "../../state.hpp"
#include "../../vulkan/context/vulkan_context.hpp"
#include "vk_images.h"

namespace sdf_raster {

namespace gui {

void vk_check_result (VkResult err) {
    VK_CHECK_RESULT (err);
}

class UI {
public:
    UI (const UI&) = delete;
    UI& operator= (const UI&) = delete;

    static UI& get_instance () {
        static UI ui;
        return ui;
    }

    void init (std::shared_ptr <VulkanContext> vulkan_context, std::shared_ptr <SceneManager> scene_manager, const InitInfo& info, Settings& settings);
    void update (Settings& settings, const Stats& stats);
    void draw (uint32_t image_index, VkCommandBuffer cmd_buff);
    void cleanup (Settings& settings);

private:
    UI () = default;

    void create_render_pass ();
    void create_depth_buffer ();
    void create_imgui_framebuffers ();

private:
    VkDevice device = VK_NULL_HANDLE;
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_index;

    std::vector <VkImageView> swapchain_image_views;
    VkExtent2D surface_extent = {0, 0};
    VkFormat surface_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    vk_utils::VulkanImageMem depth_buffer;

    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector <VkFramebuffer> framebuffers;

private:
    void init_style ();

    void key_input (Settings& settings, SceneState& scene_state);
    void handle_global_shortcuts (Settings& settings);
    void menu_bar (Settings& settings);
    void file_dialog ();
    void renderer_window (Settings& settings, const Stats& stats, SceneState& scene_state);
    void status_bar (Settings& settings, const Stats& stats, SceneState& scene_state);

private:
    ImGui::FileBrowser file_browser;

    bool show_ui = true;
    Settings previous_frame_settings;
    const float alpha = .8f;

    bool lock_occlusion_culling = false;
    bool pending_config_notify = false;

    std::shared_ptr <SceneManager> scene_manager;
};

void UI::create_render_pass () {
    vk_utils::RenderTargetInfo2D color_target_info {
        .size = this->surface_extent,
        .format = this->surface_format,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
        .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    vk_utils::RenderTargetInfo2D depth_target_info {
        .size = this->surface_extent,
        .format = this->depth_format,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };
    this->render_pass = vk_utils::createRenderPass (this->device, color_target_info, depth_target_info);
}

void UI::create_depth_buffer () {
    assert (this->physical_device != VK_NULL_HANDLE && "Physical device must be valid to create depth buffer.");
    assert (this->device != VK_NULL_HANDLE && "Device must be valid to create depth buffer.");
    assert (this->surface_extent.width > 0 && this->surface_extent.height > 0 && "Surface extent must be valid to create depth buffer.");
    assert (this->depth_format != VK_FORMAT_UNDEFINED && "Depth format must be defined to create depth buffer.");

    if (this->depth_buffer.image == VK_NULL_HANDLE) {
        vk_utils::deleteImg (this->device, &this->depth_buffer);
    }

    this->depth_buffer = vk_utils::createDepthTexture (this->device
        , this->physical_device
        , this->surface_extent.width
        , this->surface_extent.height
        , this->depth_format);
}

void UI::create_imgui_framebuffers () {
    assert (this->device != VK_NULL_HANDLE && "Device must be valid to create framebuffers.");
    assert (this->render_pass != VK_NULL_HANDLE && "ImGui RenderPass must be valid to create framebuffers.");
    assert (!this->swapchain_image_views.empty() && "Swapchain Image Views must not be empty.");
    assert (this->depth_buffer.view != VK_NULL_HANDLE && "Depth buffer view must be valid to create framebuffers.");
    assert (this->surface_extent.width > 0 && this->surface_extent.height > 0 && "Surface extent must be valid to create framebuffers.");

    this->framebuffers.resize (this->swapchain_image_views.size ());

    for (size_t i = 0; i < this->swapchain_image_views.size (); ++i) {
        std::array <VkImageView, 2> attachments;
        attachments [0] = this->swapchain_image_views [i];
        attachments [1] = this->depth_buffer.view;

        VkFramebufferCreateInfo framebuffer_info {};
        framebuffer_info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass      = this->render_pass;
        framebuffer_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
        framebuffer_info.pAttachments    = attachments.data ();
        framebuffer_info.width           = this->surface_extent.width;
        framebuffer_info.height          = this->surface_extent.height;
        framebuffer_info.layers          = 1;

        if (vkCreateFramebuffer (this->device, &framebuffer_info, nullptr, &this->framebuffers [i]) != VK_SUCCESS) {
            throw std::runtime_error ("Failed to create ImGui framebuffer for swapchain image " + std::to_string (i) + "!");
        }
    }
}

void UI::init (std::shared_ptr <VulkanContext> /*vulkan_context*/, std::shared_ptr <SceneManager> scene_manager, const InitInfo& info, Settings& settings) {
    assert (scene_manager);
    this->scene_manager = scene_manager;

    assert (info.device != VK_NULL_HANDLE && "InitInfo.device must be valid.");
    assert (info.window != nullptr && "InitInfo.window must be valid.");
    assert (info.instance != VK_NULL_HANDLE && "InitInfo.instance must be valid.");
    assert (info.physical_device != VK_NULL_HANDLE && "InitInfo.physical_device must be valid.");
    assert (info.graphics_queue != VK_NULL_HANDLE && "InitInfo.graphics_queue must be valid.");
    assert (info.graphics_queue_family_index != UINT32_MAX && "InitInfo.graphics_queue_family_index must be valid."); // UINT32_MAX обычно означает недействительный индекс
    // TODO: swapchain fields moved to PresentationContext
    // assert (!info.swapchain_image_views.empty() && "InitInfo.swapchain_image_views must not be empty.");
    // assert (info.surface_extent.width > 0 && info.surface_extent.height > 0 && "InitInfo.surface_extent must be valid.");
    // assert (info.surface_format != VK_FORMAT_UNDEFINED && "InitInfo.surface_format must be defined.");
    // assert (info.depth_format != VK_FORMAT_UNDEFINED && "InitInfo.depth_format must be defined.");

    this->device = info.device;
    this->window = info.window;
    this->instance = info.instance;
    this->physical_device = info.physical_device;
    this->queue = info.graphics_queue;
    this->graphics_queue_family_index = info.graphics_queue_family_index;
    this->swapchain_image_views = info.swapchain_image_views;
    this->surface_extent = info.surface_extent;
    this->surface_format = info.surface_format;
    this->depth_format = info.depth_format;

    this->create_depth_buffer ();
    this->create_render_pass ();
    this->create_imgui_framebuffers ();

    ImGui::CreateContext ();
    ImGuiIO& io = ImGui::GetIO ();

    int window_w, window_h;
    int framebuffer_w, framebuffer_h;
    glfwGetWindowSize (this->window, &window_w, &window_h);
    glfwGetFramebufferSize (this->window, &framebuffer_w, &framebuffer_h);

    io.DisplaySize = ImVec2 (static_cast <float> (window_w), static_cast <float> (window_h));
    io.DisplayFramebufferScale = ImVec2 (
        static_cast <float> (framebuffer_w) / static_cast <float> (window_w),
        static_cast <float> (framebuffer_h) / static_cast <float> (window_h)
    );

    ImGui_ImplVulkan_InitInfo im_init_info {};
    im_init_info.Instance = this->instance;
    im_init_info.PhysicalDevice = this->physical_device;
    im_init_info.Device = this->device;
    im_init_info.Queue = this->queue;
    im_init_info.QueueFamily = this->graphics_queue_family_index;
    im_init_info.DescriptorPool = VK_NULL_HANDLE;
    im_init_info.DescriptorPoolSize = 1000;
    im_init_info.MinImageCount = 2;
    im_init_info.ImageCount = static_cast <uint32_t> (this->swapchain_image_views.size ());
    im_init_info.PipelineInfoMain.RenderPass = this->render_pass;
    im_init_info.PipelineInfoMain.Subpass = 0;
    im_init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    im_init_info.UseDynamicRendering = VK_FALSE;
    im_init_info.PipelineCache = VK_NULL_HANDLE;
    im_init_info.Allocator = VK_NULL_HANDLE;
    im_init_info.CheckVkResultFn = vk_check_result;
    im_init_info.ApiVersion = VK_API_VERSION_1_4;

    ImGui_ImplGlfw_InitForVulkan (this->window, true);
    ImGui_ImplVulkan_Init (&im_init_info);

    this->init_style ();

    this->file_browser = ImGui::FileBrowser (0, settings.scenes_directory);
    this->file_browser.SetTitle ("Pick SDF-scene file");
    this->file_browser.SetTypeFilters (this->scene_manager->get_registered_extensions ());
}

void UI::handle_global_shortcuts (Settings& settings) {
    if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) {
        this->file_browser.Open ();
    }
    if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_1, ImGuiInputFlags_RouteGlobal)) {
        settings.show_camera_window = !settings.show_camera_window;
    }
    if (ImGui::Shortcut (ImGuiMod_Ctrl | ImGuiKey_2, ImGuiInputFlags_RouteGlobal)) {
        settings.show_renderer_window = !settings.show_renderer_window;
    }
}

void UI::menu_bar (Settings& settings) {
    if (ImGui::BeginMainMenuBar ()) {
        if (ImGui::BeginMenu ("File")) {
            ImGui::SetNextItemShortcut (ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_Tooltip);
            if (ImGui::MenuItem ("Open...", "Ctrl+O")) {
                this->file_browser.Open ();
            }
            ImGui::Separator ();
            if (ImGui::MenuItem ("Quit", "")) {
                glfwSetWindowShouldClose (this->window, true);
            }
            ImGui::EndMenu ();
        }

        if (ImGui::BeginMenu ("View")) {
            ImGui::SetNextItemShortcut (ImGuiMod_Ctrl | ImGuiKey_2, ImGuiInputFlags_Tooltip);
            if (ImGui::MenuItem ("Show Renderer Window", "Ctrl+2", &settings.show_renderer_window)) { }
            ImGui::SetNextItemShortcut (ImGuiMod_Ctrl | ImGuiKey_1, ImGuiInputFlags_Tooltip);
            if (ImGui::MenuItem ("Show Camera Window", "Ctrl+1", &settings.show_camera_window)) { }
            ImGui::EndMenu ();
        }

        ImGui::EndMainMenuBar ();
    }
}

namespace {

void camera_input (Camera& camera) {
    ImGuiIO& io = ImGui::GetIO ();

    LiteMath::float3 move {0.f, 0.f, 0.f};
    move.x += static_cast <float> (ImGui::IsKeyDown (ImGuiKey_W));
    move.x -= static_cast <float> (ImGui::IsKeyDown (ImGuiKey_S));
    move.y += static_cast <float> (ImGui::IsKeyDown (ImGuiKey_D));
    move.y -= static_cast <float> (ImGui::IsKeyDown (ImGuiKey_A));
    move.z -= static_cast <float> (ImGui::IsKeyDown (ImGuiKey_Space));
    move.z += static_cast <float> (ImGui::IsKeyDown (ImGuiKey_LeftCtrl));

    if (LiteMath::length (move) > 0.0f) {
        camera.move (LiteMath::normalize (move), io.DeltaTime);
    }

    if (!io.WantCaptureMouse) {
        camera.rotate (io.MouseDelta.x, -io.MouseDelta.y);
        camera.adjust_fov (io.MouseWheel);
    }
}

}

void UI::key_input (Settings& settings, SceneState& scene_state) {
    ImGuiIO& io = ImGui::GetIO ();

    if (io.WantCaptureKeyboard) {
        return;
    }

    if (settings.disabled_cursor) {
        camera_input (scene_state.camera);
    }

    if (ImGui::IsKeyPressed (ImGuiKey_R, false)) {
        scene_state.camera.reset ();
    }

    if (ImGui::IsKeyPressed (ImGuiKey_V, false)) {
        settings.frustum_view = !settings.frustum_view;
    }

    if (ImGui::IsKeyPressed (ImGuiKey_Escape, false)) {
        settings.disabled_cursor = false;
        glfwSetInputMode (this->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    if (ImGui::IsKeyPressed (ImGuiKey_H, false)) {
        settings.show_ui = !settings.show_ui;
    }
}

void camera_window (Camera& camera) {
    ImGui::Begin ("Camera");

    auto wrap_angle = [] (float angle) {
        angle = std::fmod (angle + 180.0f, 360.0f);
        if (angle < 0.0f)
            angle += 360.0f;
        return angle - 180.0f;
    };

    if (ImGui::CollapsingHeader ("Orientation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputFloat3 ("Position", &camera.position.x);
        if (ImGui::DragFloat ("Yaw", &camera.yaw_angle, 1.0f, 0.0f, 0.0f, "%.1f deg")) {
            camera.yaw_angle = wrap_angle (camera.yaw_angle);
        }
        ImGui::DragFloat ("Pitch", &camera.pitch_angle, 1.0f, -89.0f, 89.0f, "%.1f deg");

        ImGui::BeginDisabled ();
        ImGui::InputFloat3 ("Front", &camera.front.x);
        ImGui::InputFloat3 ("Right", &camera.right.x);
        ImGui::InputFloat3 ("Up", &camera.up.x);
        ImGui::EndDisabled ();
    }

    if (ImGui::CollapsingHeader ("Movement", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat ("Movement Speed", &camera.movement_speed, 0.01f, 0.0f, 0.5f, "%.3f");
        ImGui::DragFloat ("Mouse Sensitivity", &camera.mouse_sensitivity, 0.01f, 0.0f, 1.0f, "%.3f");
    }

    if (ImGui::CollapsingHeader ("Projection Frustum", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat ("Field of View (Y)", &camera.fov_y, 1.0f, 120.0f, "%.1f deg");

        ImGui::DragFloat ("Near Plane", &camera.near_plane, 0.005f, 0.001f, camera.far_plane, "%.3f");
        ImGui::DragFloat ("Far Plane", &camera.far_plane, 1.0f, camera.near_plane, 1000.0f, "%.2f");

        ImGui::BeginDisabled ();
        ImGui::DragFloat ("Aspect Ratio", &camera.aspect_ratio, 0.01f, 0.1f, 5.0f, "%.2f");
        ImGui::EndDisabled ();
    }

    ImGui::End ();
}

void UI::file_dialog () {
    this->file_browser.Display ();
    if (this->file_browser.HasSelected ()) {
        const auto& path = this->file_browser.GetSelected ();
        LOG_INFO ("[UI] selected SDF filename: {}", path.string ());
        this->file_browser.ClearSelected ();
        this->scene_manager->load_scene (path);
    }
}

void UI::renderer_window (Settings& settings, const Stats& stats, SceneState& scene_state) {
    ImGui::Begin ("Rendering");

    ImGui::Text ("Screen size: %dx%d pixels", this->surface_extent.width, this->surface_extent.height);

    ImGui::SeparatorText ("Render Method");

    auto scene = this->scene_manager->get_scene ();
    if (scene) {
        auto available_methods = scene->get_available_draw_methods ();
        int current_method = 0;
        for (int i = 0; i < static_cast<int>(available_methods.size ()); i++) {
            if (scene_state.draw_method == available_methods [i]) {
                current_method = i;
                break;
            }
        }
        assert (std::find (available_methods.begin (), available_methods.end (), scene_state.draw_method) != available_methods.end ());

        std::vector <const char*> method_name_ptrs;
        method_name_ptrs.reserve (available_methods.size ());
        for (auto method : available_methods) {
            method_name_ptrs.push_back (draw_method_name (method).data ());
        }

        ImGui::BeginDisabled (scene_state.draw_method == DrawMethod::None);
        if (ImGui::Combo ("##draw_method", &current_method, method_name_ptrs.data (), method_name_ptrs.size ())) {
            scene_state.draw_method = available_methods [current_method];
            this->pending_config_notify = true;
        }
        ImGui::EndDisabled ();
    } else {
        ImGui::Text ("No scene loaded");
    }

    ImGui::SeparatorText ("Level of Detail");

    ImGui::Text ("distance: %.3f", stats.distance);
    ImGui::Text ("calculated LOD level: %d", stats.lod);
    ImGui::InputInt ("max lod", &scene_state.max_lod);
    if (scene_state.octree_depth) {
        scene_state.max_lod = LiteMath::clamp (scene_state.max_lod, scene_state.cpu_traversed, scene_state.octree_depth);
    } else {
        scene_state.max_lod = LiteMath::max (scene_state.max_lod, scene_state.cpu_traversed);
    }

    ImGui::SeparatorText ("Octree");
    ImGui::Text ("octree depth: %d/%d", scene_state.max_lod, scene_state.octree_depth);

    ImGui::Text ("gpu traversed: %d", scene_state.max_lod - scene_state.cpu_traversed);
    ImGui::Text ("cpu traversed:");
    if (ImGui::IsItemHovered ()) {
        ImGui::SetTooltip ("Levels to descend on cpu prior GPU.");
    }
    for (int i = 0; i <= 5; i++) {
        ImGui::SameLine ();
        if (ImGui::RadioButton (std::to_string (i).c_str (), &scene_state.cpu_traversed, i)) {
            this->pending_config_notify = true;
        }
    }

    ImGui::SeparatorText ("Culling");

    ImGui::InputInt ("frustum", &scene_state.frustum_culling_level);
    if (scene_state.octree_depth) {
        scene_state.frustum_culling_level = LiteMath::clamp (scene_state.frustum_culling_level, scene_state.cpu_traversed, scene_state.octree_depth);
    } else if (scene_state.cpu_traversed) {
        scene_state.frustum_culling_level = LiteMath::max (scene_state.frustum_culling_level, scene_state.cpu_traversed);
    }
    if (ImGui::IsItemHovered ()) {
        ImGui::SetTooltip ("Disables rendering of objects outside the camera's view frustum.");
    }

    if (!this->previous_frame_settings.frustum_view && settings.frustum_view && !scene_state.occlusion_culling_level) {
        LOG_WARN ("[UI] Frustum view mode entered w/o occlusion culling. Toggling disabled: depth data missing.");
        this->lock_occlusion_culling = true;
    } else if (!settings.frustum_view) {
        this->lock_occlusion_culling = false;
    }

    if (this->lock_occlusion_culling) {
        ImGui::BeginDisabled ();
    }

    ImGui::InputInt ("occlusion", &scene_state.occlusion_culling_level);
    if (scene_state.octree_depth) {
        scene_state.occlusion_culling_level = LiteMath::clamp (scene_state.occlusion_culling_level, 0, scene_state.octree_depth);
    }
    if (ImGui::IsItemHovered (ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip ();
        ImGui::SetTooltip ("Disables rendering of objects hidden behind other objects.");
        if (this->lock_occlusion_culling) {
            ImGui::TextColored (ImVec4 (1.0f, 1.0f, 0.0f, 1.0f), "Frustum view mode entered w/o occlusion culling. Depth data missing.");
        }
        ImGui::EndTooltip ();
    }

    if (this->lock_occlusion_culling) {
        ImGui::EndDisabled ();
    }

    ImGui::SeparatorText ("Lighting");
    ImGui::Checkbox ("color leafs", &settings.color_leafs);

    auto& lighting = settings.lighting;

    ImGui::Spacing ();

    if (ImGui::CollapsingHeader ("Deferred lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3 ("light pos", &lighting.light_pos.x, 0.1f);
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("World-space light position.");
        }

        ImGui::ColorEdit3 ("light color", &lighting.light_color.x);
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Light color used for diffuse and specular terms.");
        }

        ImGui::SliderFloat ("ambient strength", &lighting.ambient_strength, 0.0f, 1.0f, "%.3f");
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Constant ambient light contribution.");
        }

        ImGui::SliderFloat ("specular strength", &lighting.specular_strength, 0.0f, 2.0f, "%.3f");
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Intensity of the specular highlight.");
        }

        ImGui::SliderFloat ("shininess", &lighting.shininess, 1.0f, 256.0f, "%.1f");
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Higher values produce a tighter specular highlight.");
        }

        ImGui::DragFloat ("depth threshold", &lighting.depth_threshold, 0.00001f, 0.0f, 0.01f, "%.5f");
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Threshold for hole detection when selecting a better depth neighbor.");
        }

        ImGui::ColorEdit3 ("fog color", &lighting.fog_color.x);
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Fog color blended with lit color by depth.");
        }

        ImGui::ColorEdit4 ("clear color", &lighting.clear_color.x);
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Background clear color.");
        }

        ImGui::DragFloat ("fog start", &lighting.fog_start, 0.0001f, 0.0f, 1.0f, "%.4f");
        ImGui::DragFloat ("fog end", &lighting.fog_end, 0.0001f, 0.0f, 1.0f, "%.4f");
        if (ImGui::IsItemHovered ()) {
            ImGui::SetTooltip ("Fog range end depth.");
        }

        lighting.fog_start = LiteMath::clamp (lighting.fog_start, 0.0f, 1.0f);
        lighting.fog_end   = LiteMath::clamp (lighting.fog_end,   0.0f, 1.0f);

        if (lighting.fog_start > lighting.fog_end) {
            std::swap (lighting.fog_start, lighting.fog_end);
        }

        lighting.ambient_strength  = LiteMath::max (lighting.ambient_strength,  0.0f);
        lighting.specular_strength = LiteMath::max (lighting.specular_strength, 0.0f);
        lighting.shininess         = LiteMath::max (lighting.shininess,         1.0f);
        lighting.depth_threshold   = LiteMath::max (lighting.depth_threshold,   0.0f);

        if (ImGui::Button ("reset lighting")) {
            lighting.light_pos          = LiteMath::float3 (5.0f, 5.0f, 5.0f);
            lighting.light_color        = LiteMath::float3 (1.0f, 1.0f, 1.0f);
            lighting.fog_color          = LiteMath::float3 (0.25f, 0.25f, 0.25f);
            lighting.clear_color        = LiteMath::float4 (0.25f, 0.25f, 0.25f, 1.0f);
            lighting.ambient_strength   = 0.1f;
            lighting.specular_strength  = 0.4f;
            lighting.shininess          = 64.0f;
            lighting.depth_threshold    = 0.0001f;
            lighting.fog_start          = 0.999f;
            lighting.fog_end            = 1.0f;
        }
    }

    ImGui::End ();
}

void UI::status_bar (Settings& settings, const Stats& stats, SceneState& scene_state) {
    ImGuiIO& io = ImGui::GetIO ();

    struct StatusBarElement {
        std::string text;
    };

    std::vector <StatusBarElement> elements;
    elements.push_back (StatusBarElement {
        .text = std::format ("Scene:{}", scene_state.name)
    });
    elements.push_back (StatusBarElement {
        .text = std::format ("Method:{}", scene_state.draw_method)
    });
    elements.push_back (StatusBarElement {
        .text = std::format ("Mode:{}", (settings.frustum_view) ? "frustum" : "camera")
    });
    elements.push_back (StatusBarElement {
        .text = std::format ("Cursor:{}", (settings.disabled_cursor) ? "disabled" : "normal")
    });

    const auto& method = scene_state.draw_method;
    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeComputeDeferred
        || method == DrawMethod::SComTreeMesh || method == DrawMethod::SComTreeMeshDeferred
        || method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        elements.push_back (StatusBarElement {
            .text = std::format ("Roots:{:06}", stats.active_roots_count)
        });
        elements.push_back (StatusBarElement {
            .text = std::format ("Leafs:{:06}", stats.active_leafs_count)
        });
    }

    elements.push_back (StatusBarElement {
        .text = std::format ("FPS:{:.1f}", io.Framerate)
    });

    ImGuiViewport* viewport = ImGui::GetMainViewport ();
    ImVec2 viewport_pos = viewport->Pos;
    ImVec2 viewport_size = viewport->Size;

    float status_bar_height = ImGui::GetFrameHeight () * 1.2f;

    ImVec2 status_bar_pos = ImVec2 (viewport_pos.x, viewport_pos.y + viewport_size.y - status_bar_height);
    ImVec2 status_bar_size = ImVec2 (viewport_size.x, status_bar_height);

    ImGui::SetNextWindowPos (status_bar_pos);
    ImGui::SetNextWindowSize (status_bar_size);

    ImGuiWindowFlags status_bar_flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoNavInputs
        | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, ImVec2 (8, 4));

    if (ImGui::Begin ("##StatusBar", nullptr, status_bar_flags)) {
        for (size_t i = 0; i < elements.size (); ++i) {
            ImGui::Text ("%s", elements [i].text.c_str ());
            if (i < elements.size () - 1) {
                ImGui::SameLine ();
                ImGui::Text ("|");
                ImGui::SameLine ();
            }
        }
    }
    ImGui::End ();
    ImGui::PopStyleVar ();
}

void UI::update (Settings& settings, const Stats& stats) {
    ImGuiIO& io = ImGui::GetIO ();
    if (settings.disabled_cursor) {
        ImGui::SetWindowFocus (NULL);
        io.WantCaptureMouse = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        io.WantCaptureMouse = true;
        io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    ImGui_ImplVulkan_NewFrame ();
    ImGui_ImplGlfw_NewFrame ();

    ImGui::NewFrame ();

    if (this->scene_manager->is_loading_scene ()) {
        this->menu_bar (settings);
        this->file_dialog ();

        ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
        if (ImGui::Begin ("Loading...", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text ("Loading scene");
            ImGui::End ();
        }
        return;
    }

    auto current_scene = this->scene_manager->get_scene ();

    if (current_scene) {
        SceneState& scene_state = current_scene->get_state ();

        this->key_input (settings, scene_state);
        this->show_ui = settings.show_ui;

        if (this->show_ui) {
            this->handle_global_shortcuts (settings);
            this->menu_bar (settings);
            this->file_dialog ();

            if (settings.show_camera_window) {
                camera_window (scene_state.camera);
            }
            if (settings.show_renderer_window) {
                this->renderer_window (settings, stats, scene_state);
            }
            this->status_bar (settings, stats, scene_state);
        }
        scene_state.camera.update ();
    } else {
        if (settings.show_ui) {
            this->handle_global_shortcuts (settings);
            this->menu_bar (settings);
            this->file_dialog ();

            ImGui::SetNextWindowPos (ImGui::GetMainViewport ()->GetCenter (), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));
            if (ImGui::Begin ("Welcome", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text ("No scene loaded. Press Ctrl+O to open a scene.");
                if (ImGui::Button ("Open Scene...")) {
                    this->file_browser.Open ();
                }
                ImGui::End ();
            }
        }
    }

    if (this->pending_config_notify) {
        this->scene_manager->notify (SceneEventType::CONFIG_CHANGED);
        this->pending_config_notify = false;
    }

    this->previous_frame_settings = settings;
}

void UI::draw (uint32_t image_index, VkCommandBuffer cmd_buff) {
    if (!this->show_ui) {
        ImGui::EndFrame ();
        return;
    }

    if (image_index >= this->framebuffers.size ()) {
        throw std::out_of_range ("Invalid image index for ImGui framebuffer. Index: " + std::to_string (image_index) +
                                ", available framebuffers: " + std::to_string (this->framebuffers.size ()));
    }
    VkFramebuffer current_framebuffer = this->framebuffers [image_index];

    std::array <VkClearValue, 2> clear_value {};
    clear_value [0].color.float32 [0] = 0.0f; clear_value [0].color.float32 [1] = 0.0f;
    clear_value [0].color.float32 [2] = 0.0f; clear_value [0].color.float32 [3] = 0.0f;
    clear_value [1].depthStencil.depth = 1.0f;
    clear_value [1].depthStencil.stencil = 0;

    VkRenderPassBeginInfo render_pass_begin {};
    render_pass_begin.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin.renderPass      = this->render_pass;
    render_pass_begin.framebuffer     = current_framebuffer;
    render_pass_begin.renderArea.offset = {0, 0};
    render_pass_begin.renderArea.extent = this->surface_extent;
    render_pass_begin.clearValueCount = static_cast <uint32_t> (clear_value.size ());
    render_pass_begin.pClearValues    = clear_value.data ();

    ImGui::Render ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData (ImGui::GetDrawData (), cmd_buff);
    vkCmdEndRenderPass (cmd_buff);
}

void UI::cleanup (Settings& settings) {
    settings.scenes_directory = this->file_browser.GetDirectory ();

    if (this->device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle (this->device);
    ImGui_ImplVulkan_Shutdown ();
    ImGui_ImplGlfw_Shutdown ();
    ImGui::DestroyContext ();

    if (this->device == VK_NULL_HANDLE) {
        return;
    }

    for (VkFramebuffer fb : this->framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->device, fb, nullptr);
        }
    }
    this->framebuffers.clear ();

    if (this->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass (this->device, this->render_pass, nullptr);
        this->render_pass = VK_NULL_HANDLE;
    }

    vk_utils::deleteImg (this->device, &this->depth_buffer);

    this->swapchain_image_views.clear ();
    this->device = VK_NULL_HANDLE;
    this->window = nullptr;
    this->instance = VK_NULL_HANDLE;
    this->physical_device = VK_NULL_HANDLE;
    this->queue = VK_NULL_HANDLE;
}

void UI::init_style () {
    ImGuiStyle& style = ImGui::GetStyle ();
    ImGuiIO& io = ImGui::GetIO ();

    style.FontSizeBase = 12.0f;
    static ImWchar exclude_ranges [] = { '0', '9', 0 };

    ImFontConfig cfg1;
    cfg1.GlyphExcludeRanges = exclude_ranges;
    ImFont* font1 = io.Fonts->AddFontFromFileTTF ("./assets/fonts/CodecPro-Regular.ttf", 0.0f, &cfg1);
    IM_ASSERT (font1 != nullptr);

    ImFontConfig cfg2;
    cfg2.MergeMode = true;
    cfg2.ExtraSizeScale = 1.2f;
    ImFont* font2 = io.Fonts->AddFontFromFileTTF ("./assets/fonts/FiraMono-Medium.ttf", 0.0f, &cfg2);
    IM_ASSERT (font2 != nullptr);

    // light style from Pacôme Danhiez (user itamago) https://github.com/ocornut/imgui/pull/511#issuecomment-175719267
    style.Alpha = 1.0f;
    style.FrameRounding = 3.0f;
    style.Colors [ImGuiCol_Text]                  = ImVec4 (0.00f, 0.00f, 0.00f, 1.00f);
    style.Colors [ImGuiCol_TextDisabled]          = ImVec4 (0.60f, 0.60f, 0.60f, 1.00f);
    style.Colors [ImGuiCol_WindowBg]              = ImVec4 (0.94f, 0.94f, 0.94f, 0.94f);
    style.Colors [ImGuiCol_ChildBg]               = ImVec4 (0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors [ImGuiCol_PopupBg]               = ImVec4 (1.00f, 1.00f, 1.00f, 0.94f);
    style.Colors [ImGuiCol_Border]                = ImVec4 (0.00f, 0.00f, 0.00f, 0.39f);
    style.Colors [ImGuiCol_BorderShadow]          = ImVec4 (1.00f, 1.00f, 1.00f, 0.10f);
    style.Colors [ImGuiCol_FrameBg]               = ImVec4 (1.00f, 1.00f, 1.00f, 0.94f);
    style.Colors [ImGuiCol_FrameBgHovered]        = ImVec4 (0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors [ImGuiCol_FrameBgActive]         = ImVec4 (0.26f, 0.59f, 0.98f, 0.67f);
    style.Colors [ImGuiCol_TitleBg]               = ImVec4 (0.96f, 0.96f, 0.96f, 1.00f);
    style.Colors [ImGuiCol_TitleBgCollapsed]      = ImVec4 (1.00f, 1.00f, 1.00f, 0.51f);
    style.Colors [ImGuiCol_TitleBgActive]         = ImVec4 (0.82f, 0.82f, 0.82f, 1.00f);
    style.Colors [ImGuiCol_MenuBarBg]             = ImVec4 (0.86f, 0.86f, 0.86f, 1.00f);
    style.Colors [ImGuiCol_ScrollbarBg]           = ImVec4 (0.98f, 0.98f, 0.98f, 0.53f);
    style.Colors [ImGuiCol_ScrollbarGrab]         = ImVec4 (0.69f, 0.69f, 0.69f, 1.00f);
    style.Colors [ImGuiCol_ScrollbarGrabHovered]  = ImVec4 (0.59f, 0.59f, 0.59f, 1.00f);
    style.Colors [ImGuiCol_ScrollbarGrabActive]   = ImVec4 (0.49f, 0.49f, 0.49f, 1.00f);
    style.Colors [ImGuiCol_ChildBg]               = ImVec4 (0.86f, 0.86f, 0.86f, 0.99f);
    style.Colors [ImGuiCol_CheckMark]             = ImVec4 (0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_SliderGrab]            = ImVec4 (0.24f, 0.52f, 0.88f, 1.00f);
    style.Colors [ImGuiCol_SliderGrabActive]      = ImVec4 (0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_Button]                = ImVec4 (0.26f, 0.59f, 0.98f, 0.40f);
    style.Colors [ImGuiCol_ButtonHovered]         = ImVec4 (0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_ButtonActive]          = ImVec4 (0.06f, 0.53f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_Header]                = ImVec4 (0.26f, 0.59f, 0.98f, 0.31f);
    style.Colors [ImGuiCol_HeaderHovered]         = ImVec4 (0.26f, 0.59f, 0.98f, 0.80f);
    style.Colors [ImGuiCol_HeaderActive]          = ImVec4 (0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_Separator]             = ImVec4 (0.39f, 0.39f, 0.39f, 1.00f);
    style.Colors [ImGuiCol_SeparatorHovered]      = ImVec4 (0.26f, 0.59f, 0.98f, 0.78f);
    style.Colors [ImGuiCol_SeparatorActive]       = ImVec4 (0.26f, 0.59f, 0.98f, 1.00f);
    style.Colors [ImGuiCol_ResizeGrip]            = ImVec4 (1.00f, 1.00f, 1.00f, 0.50f);
    style.Colors [ImGuiCol_ResizeGripHovered]     = ImVec4 (0.26f, 0.59f, 0.98f, 0.67f);
    style.Colors [ImGuiCol_ResizeGripActive]      = ImVec4 (0.26f, 0.59f, 0.98f, 0.95f);
    style.Colors [ImGuiCol_PlotLines]             = ImVec4 (0.39f, 0.39f, 0.39f, 1.00f);
    style.Colors [ImGuiCol_PlotLinesHovered]      = ImVec4 (1.00f, 0.43f, 0.35f, 1.00f);
    style.Colors [ImGuiCol_PlotHistogram]         = ImVec4 (0.90f, 0.70f, 0.00f, 1.00f);
    style.Colors [ImGuiCol_PlotHistogramHovered]  = ImVec4 (1.00f, 0.60f, 0.00f, 1.00f);
    style.Colors [ImGuiCol_TextSelectedBg]        = ImVec4 (0.26f, 0.59f, 0.98f, 0.35f);
    style.Colors [ImGuiCol_ModalWindowDimBg]      = ImVec4 (0.20f, 0.20f, 0.20f, 0.35f);

    if (true) { // TODO: set in config?
        for (int i = 0; i <= ImGuiCol_COUNT; i++) {
            ImVec4& col = style.Colors [i];
            float H, S, V;
            ImGui::ColorConvertRGBtoHSV ( col.x, col.y, col.z, H, S, V );

            if (S < 0.1f) {
                V = 1.0f - V;
            }
            ImGui::ColorConvertHSVtoRGB ( H, S, V, col.x, col.y, col.z );
            if ( col.w < 1.00f ) {
                col.w *= this->alpha;
            }
        }
    } else {
        for (int i = 0; i <= ImGuiCol_COUNT; i++) {
            ImVec4& col = style.Colors [i];
            if ( col.w < 1.00f ) {
                col.x *= this->alpha;
                col.y *= this->alpha;
                col.z *= this->alpha;
                col.w *= this->alpha;
            }
        }
    }
}

void init (std::shared_ptr <VulkanContext> vulkan_context, std::shared_ptr <SceneManager> scene_manager, const InitInfo& info, Settings& settings) {
    UI::get_instance ().init (vulkan_context, scene_manager, info, settings);
}

void update (Settings& settings, const Stats& stats) {
    UI::get_instance ().update (settings, stats);
}

void draw (uint32_t image_index, VkCommandBuffer cmd_buff) {
    UI::get_instance ().draw (image_index, cmd_buff);
}

void cleanup (Settings& settings) {
    UI::get_instance ().cleanup (settings);
}

}
}

