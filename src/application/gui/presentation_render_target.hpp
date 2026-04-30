// application/gui/presentation_render_target.hpp
#pragma once

#include "../../vulkan/context/vulkan_context.hpp"
#include "vk_images.h"
#include "vk_swapchain.h"
#include "../../engine/render_target.hpp"

#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <vector>

struct GLFWwindow;

namespace sdf_raster {

class PresentationRenderTarget : public RenderTarget {
public:
    PresentationRenderTarget (std::shared_ptr <VulkanContext> a_context, GLFWwindow* a_window);
    virtual ~PresentationRenderTarget ();

    virtual void shutdown () override;
    virtual void on_before_device_wait_idle () {}

    inline bool is_initialized () const override { return this->initialized; }

    inline VkExtent2D get_extent () const override { return this->swapchain.GetExtent (); }
    inline VkFormat get_image_format () const override { return this->swapchain.GetFormat (); }
    inline uint32_t get_image_count () const override { return this->swapchain.GetImageCount (); }
    inline std::vector <VkImageView> get_image_views () override {
        std::vector <VkImageView> views (this->swapchain.GetImageCount ());
        for (size_t i = 0; i < views.size (); i++) {
            views [i] = this->swapchain.GetAttachment (static_cast <uint32_t> (i)).view;
        }
        return views;
    }
    inline VkImageLayout get_output_final_layout () const override { return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; }

    VkCommandBuffer begin_frame (uint32_t a_frame_index) override;
    void end_frame (VkCommandBuffer a_cmd_buff, uint32_t a_frame_index) override;
    inline uint32_t get_max_frames_in_flight () const override { return this->max_frames_in_flight; }
    inline uint32_t get_current_image_index () const override { return this->acquired_image_index; }
    inline uint32_t get_swapchain_image_index () const { return this->acquired_image_index; }

    inline void register_resizable (std::function <void ()> a_func) {
        this->resizable_callbacks.push_back (a_func);
    }
    inline void set_resized_flag () { this->framebuffer_resized = true; }

    inline VkQueue get_present_queue () const { return this->present_queue; }
    inline VkPhysicalDevice get_physical_device () const { return this->context->get_physical_device (); }
    inline VkDevice get_device () const { return this->context->get_device (); }

protected:
    void resize ();

    void create_surface ();
    void create_swapchain (uint32_t a_width, uint32_t a_height);
    void create_frame_resources ();
    void destroy_swapchain ();
    void destroy_frame_resources ();

    std::shared_ptr <VulkanContext> context = nullptr;
    GLFWwindow* window = nullptr;

    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VulkanSwapChain swapchain;
    VkQueue present_queue = VK_NULL_HANDLE;
    std::vector <VkSemaphore> gpu_ready_to_present;
    size_t frames_in_swapchain = 3;
    uint32_t acquired_image_index = 0;
    bool framebuffer_resized = false;

    struct FrameResources {
        VkFence in_flight_fence = VK_NULL_HANDLE;
        VkSemaphore image_available = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    };
    std::vector <FrameResources> frame_resources;
    const size_t max_frames_in_flight = 2;

    std::vector <std::function <void ()>> resizable_callbacks;

    bool initialized = false;
};

} // namespace sdf_raster
