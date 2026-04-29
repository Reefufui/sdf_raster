// vulkan/presentation/presentation_context.cpp
#include "presentation_context.hpp"
#include "logger.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sdf_raster {

PresentationContext::PresentationContext (std::shared_ptr <VulkanContext> a_context, GLFWwindow* a_window)
    : context (a_context)
    , window (a_window) {
    this->create_surface ();
    int width, height;
    glfwGetFramebufferSize (this->window, &width, &height);
    this->create_swapchain (static_cast <uint32_t> (width), static_cast <uint32_t> (height));
    this->create_frame_resources ();
    this->initialized = true;
}

PresentationContext::~PresentationContext () {
    this->shutdown ();
}

void PresentationContext::shutdown () {
    if (!this->initialized) {
        LOG_WARN ("[PresentationContext] Attempted to shutdown an uninitialized or already shut down presentation context.");
        return;
    }

    if (this->context->get_device () == VK_NULL_HANDLE) {
        LOG_WARN ("[PresentationContext] Vulkan device was VK_NULL_HANDLE during shutdown. Resources might not have been created.");
    } else {
        LOG_INFO ("[PresentationContext] Waiting for the GPU to go idle to shutdown application.");
        this->on_before_device_wait_idle ();
        vkDeviceWaitIdle (this->context->get_device ());

        this->destroy_swapchain ();
        this->destroy_frame_resources ();
    }

    if (this->surface != VK_NULL_HANDLE) {
        if (this->context->get_instance () != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR (this->context->get_instance (), this->surface, nullptr);
        } else {
            LOG_ERROR ("[PresentationContext] VkInstance was VK_NULL_HANDLE while destroying VkSurfaceKHR.");
        }
        this->surface = VK_NULL_HANDLE;
    }

    this->initialized = false;
    LOG_INFO ("[PresentationContext] Presentation context destroyed successfully.");
}

void PresentationContext::create_surface () {
    VK_CHECK_RESULT (glfwCreateWindowSurface (this->context->get_instance (), this->window, nullptr, &this->surface));
}

void PresentationContext::create_swapchain (uint32_t width, uint32_t height) {
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
    LOG_INFO ("[PresentationContext] Created {} swapchain images with size ({}, {}).", this->frames_in_swapchain, extent.width, extent.height);
}

void PresentationContext::create_frame_resources () {
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

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = nullptr;
    submit_info.commandBufferCount = 0;
    submit_info.pCommandBuffers = nullptr;
    submit_info.signalSemaphoreCount = 1;

    VkQueue graphics_queue = this->context->get_graphics_queue ();
    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->context->get_device (), &semaphore_info, nullptr, &this->frame_resources [i].wait_before_color_attachment_output));
        VK_CHECK_RESULT (vkCreateSemaphore (this->context->get_device (), &semaphore_info, nullptr, &this->frame_resources [i].wait_before_depth_copy));
        VK_CHECK_RESULT (vkCreateFence (this->context->get_device (), &fence_info, nullptr, &this->frame_resources [i].cpu_wait_next_frame));
        VK_CHECK_RESULT (vkAllocateCommandBuffers (this->context->get_device (), &alloc_info, &this->frame_resources [i].command_buffer));

        submit_info.pSignalSemaphores = &this->frame_resources [i].wait_before_depth_copy;
        VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    }
}

VkCommandBuffer PresentationContext::begin_frame (uint32_t frame_idx) {
    vkWaitForFences (this->context->get_device (), 1, &this->frame_resources [frame_idx].cpu_wait_next_frame, VK_TRUE, UINT64_MAX);

    VkResult result = this->swapchain.AcquireNextImage (this->frame_resources [frame_idx].wait_before_color_attachment_output, &this->acquired_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || this->framebuffer_resized) {
        this->resize ();
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error ("failed to acquire swap chain image!");
    }

    vkResetFences (this->context->get_device (), 1, &this->frame_resources [frame_idx].cpu_wait_next_frame);
    vkResetCommandBuffer (this->frame_resources [frame_idx].command_buffer, 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VK_CHECK_RESULT (vkBeginCommandBuffer (this->frame_resources [frame_idx].command_buffer, &begin_info));

    return this->frame_resources [frame_idx].command_buffer;
}

void PresentationContext::end_frame (VkCommandBuffer command_buffer, uint32_t frame_idx) {
    assert (this->acquired_image_index < this->frames_in_swapchain);

    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    VK_CHECK_RESULT (vkEndCommandBuffer (command_buffer));

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    constexpr size_t wait_semaphores_count = 2;

    std::array <VkSemaphore, wait_semaphores_count> wait_semaphores {};
    wait_semaphores [0] = this->frame_resources [frame_idx].wait_before_color_attachment_output;
    wait_semaphores [1] = this->frame_resources [frame_idx].wait_before_depth_copy;

    std::array <VkPipelineStageFlags, wait_semaphores_count> wait_stages {};
    wait_stages [0] = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    wait_stages [1] = VK_PIPELINE_STAGE_TRANSFER_BIT;

    submit_info.waitSemaphoreCount = wait_semaphores_count;
    submit_info.pWaitSemaphores = wait_semaphores.data ();
    submit_info.pWaitDstStageMask = wait_stages.data ();

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    constexpr size_t signal_semaphores_count = 2;

    std::array <VkSemaphore, signal_semaphores_count> signal_semaphores {};
    signal_semaphores [0] = this->gpu_ready_to_present [this->acquired_image_index];
    signal_semaphores [1] = this->frame_resources [frame_idx].wait_before_depth_copy;

    submit_info.signalSemaphoreCount = signal_semaphores_count;
    submit_info.pSignalSemaphores = signal_semaphores.data ();

    VkQueue graphics_queue = this->context->get_graphics_queue ();
    VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, this->frame_resources [frame_idx].cpu_wait_next_frame));

    VkResult result = this->swapchain.QueuePresent (this->present_queue, this->acquired_image_index, this->gpu_ready_to_present [this->acquired_image_index]);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        this->resize ();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to present swap chain image!");
    }
}

void PresentationContext::resize () {
    this->framebuffer_resized = false;

    int width, height;
    glfwGetFramebufferSize (this->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents ();
        glfwGetFramebufferSize (this->window, &width, &height);
    }

    const auto extent = this->swapchain.GetExtent ();
    LOG_INFO ("[PresentationContext] Waiting device: size ({}, {}) is outdated. New window framebuffer size is ({}, {}).", extent.width, extent.height, width, height);
    vkDeviceWaitIdle (this->context->get_device ());

    this->create_swapchain (static_cast <uint32_t> (width), static_cast <uint32_t> (height));
    this->create_frame_resources ();

    for (const auto& callback : this->resizable_callbacks) {
        callback ();
    }
}

void PresentationContext::destroy_swapchain () {
    this->swapchain.Cleanup ();

    for (auto render_finished_semaphore : this->gpu_ready_to_present) {
        if (render_finished_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore (this->context->get_device (), render_finished_semaphore, nullptr);
        }
    }

    this->gpu_ready_to_present.clear ();
}

void PresentationContext::destroy_frame_resources () {
    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        vkDestroySemaphore (this->context->get_device (), this->frame_resources [i].wait_before_color_attachment_output, nullptr);
        vkDestroySemaphore (this->context->get_device (), this->frame_resources [i].wait_before_depth_copy, nullptr);
        vkDestroyFence (this->context->get_device (), this->frame_resources [i].cpu_wait_next_frame, nullptr);
    }
    this->frame_resources.clear ();
}

} // namespace sdf_raster
