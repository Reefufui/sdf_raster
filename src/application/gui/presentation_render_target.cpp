// application/gui/presentation_render_target.cpp
#include "presentation_render_target.hpp"
#include "logger.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sdf_raster {

PresentationRenderTarget::PresentationRenderTarget (std::shared_ptr <VulkanContext> a_context, GLFWwindow* a_window)
    : context (a_context)
    , window (a_window) {
    this->create_surface ();
    int width, height;
    glfwGetFramebufferSize (this->window, &width, &height);
    this->create_swapchain (static_cast <uint32_t> (width), static_cast <uint32_t> (height));
    this->create_frame_resources ();
}

PresentationRenderTarget::~PresentationRenderTarget () {
    if (this->context->get_device () == VK_NULL_HANDLE) {
        LOG_WARN ("[PresentationRenderTarget] Vulkan device was VK_NULL_HANDLE during shutdown. Resources might not have been created.");
    } else {
        LOG_INFO ("[PresentationRenderTarget] Waiting for the GPU to go idle to shutdown application.");
        vkDeviceWaitIdle (this->context->get_device ());

        this->destroy_swapchain ();
        this->destroy_frame_resources ();
    }

    if (this->surface != VK_NULL_HANDLE) {
        if (this->context->get_instance () != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR (this->context->get_instance (), this->surface, nullptr);
        } else {
            LOG_ERROR ("[PresentationRenderTarget] VkInstance was VK_NULL_HANDLE while destroying VkSurfaceKHR.");
        }
        this->surface = VK_NULL_HANDLE;
    }

    LOG_INFO ("[PresentationRenderTarget] Presentation context destroyed successfully.");
}

void PresentationRenderTarget::create_surface () {
    VK_CHECK_RESULT (glfwCreateWindowSurface (this->context->get_instance (), this->window, nullptr, &this->surface));
}

void PresentationRenderTarget::create_swapchain (uint32_t width, uint32_t height) {
    this->destroy_swapchain ();

    this->present_queue = this->swapchain.CreateSwapChain (this->context->get_physical_device ()
        , this->context->get_device ()
        , this->surface
        , width
        , height
        , this->frames_in_swapchain
        , false);

    this->frames_in_swapchain = this->swapchain.GetImageCount ();

    this->gpu_ready_to_present.resize (this->frames_in_swapchain);

    for (size_t i = 0; i < this->gpu_ready_to_present.size (); i++) {
        VkSemaphoreCreateInfo semaphoreInfo {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore (this->context->get_device (), &semaphoreInfo, nullptr, &this->gpu_ready_to_present [i]);
    }

    this->acquired_image_index = std::numeric_limits <uint32_t>::max ();

    const auto extent = this->swapchain.GetExtent ();
    LOG_INFO ("[PresentationRenderTarget] Created {} swapchain images with size ({}, {}).", this->frames_in_swapchain, extent.width, extent.height);
}

void PresentationRenderTarget::create_frame_resources () {
    if (this->frame_resources.size ()) {
        this->destroy_frame_resources ();
    }

    this->frame_resources.resize (this->max_frames_in_flight);

    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = this->context->get_graphics_command_pool_reset ();
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->context->get_device (), &semaphore_info, nullptr, &this->frame_resources [i].image_available));
        VK_CHECK_RESULT (vkCreateFence (this->context->get_device (), &fence_info, nullptr, &this->frame_resources [i].in_flight_fence));
        VK_CHECK_RESULT (vkAllocateCommandBuffers (this->context->get_device (), &alloc_info, &this->frame_resources [i].command_buffer));
    }
}

VkCommandBuffer PresentationRenderTarget::begin_frame (uint32_t frame_idx) {
    auto& frame = this->frame_resources [frame_idx];

    vkWaitForFences (this->context->get_device (), 1, &frame.in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences (this->context->get_device (), 1, &frame.in_flight_fence);

    VkResult result = this->swapchain.AcquireNextImage (frame.image_available, &this->acquired_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || this->framebuffer_resized) {
        this->resize ();
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error ("failed to acquire swap chain image!");
    }

    vkResetCommandBuffer (frame.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = 0,
        .pInheritanceInfo = nullptr
    };

    VK_CHECK_RESULT (vkBeginCommandBuffer (frame.command_buffer, &begin_info));

    return frame.command_buffer;
}

void PresentationRenderTarget::end_frame (VkCommandBuffer command_buffer, uint32_t frame_idx) {
    auto& frame = frame_resources [frame_idx];

    assert (this->acquired_image_index < this->frames_in_swapchain);

    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    VK_CHECK_RESULT (vkEndCommandBuffer (command_buffer));

    VkPipelineStageFlags wait_stages [] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submit_info {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.image_available,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &this->gpu_ready_to_present [this->acquired_image_index]
    };

    VkQueue graphics_queue = this->context->get_graphics_queue ();
    VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, frame.in_flight_fence));

    VkResult result = this->swapchain.QueuePresent (this->present_queue, this->acquired_image_index, this->gpu_ready_to_present [this->acquired_image_index]);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        this->resize ();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to present swap chain image!");
    }
}

void PresentationRenderTarget::resize () {
    this->framebuffer_resized = false;

    int width, height;
    glfwGetFramebufferSize (this->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents ();
        glfwGetFramebufferSize (this->window, &width, &height);
    }

    const auto extent = this->swapchain.GetExtent ();
    LOG_INFO ("[PresentationRenderTarget] Waiting device: size ({}, {}) is outdated. New window framebuffer size is ({}, {}).", extent.width, extent.height, width, height);
    vkDeviceWaitIdle (this->context->get_device ());

    this->create_swapchain (static_cast <uint32_t> (width), static_cast <uint32_t> (height));
    this->create_frame_resources ();

    for (const auto& callback : this->resizable_callbacks) {
        callback ();
    }
}

void PresentationRenderTarget::destroy_swapchain () {
    this->swapchain.Cleanup ();

    for (auto render_finished_semaphore : this->gpu_ready_to_present) {
        if (render_finished_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore (this->context->get_device (), render_finished_semaphore, nullptr);
        }
    }

    this->gpu_ready_to_present.clear ();
}

void PresentationRenderTarget::destroy_frame_resources () {
    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        vkDestroySemaphore (this->context->get_device (), this->frame_resources [i].image_available, nullptr);
        vkDestroyFence (this->context->get_device (), this->frame_resources [i].in_flight_fence, nullptr);
    }
    this->frame_resources.clear ();
}

} // namespace sdf_raster
