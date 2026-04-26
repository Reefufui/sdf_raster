#include "gui/presentation_context.hpp"
#include "logger.hpp"

#include <algorithm>
#include <array>
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
    if (!vk_utils::getSupportedDepthFormat (this->context->get_physical_device (), {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}, &this->depth_format)) {
        throw std::runtime_error ("couldn't find supported depth format");
    }
    this->main.render_pass = this->create_render_pass (VK_ATTACHMENT_LOAD_OP_CLEAR);
    this->after.render_pass = this->create_render_pass (VK_ATTACHMENT_LOAD_OP_NONE);
    this->create_depth_buffers ();
    this->main.framebuffer = this->create_framebuffers (this->main.render_pass);
    this->after.framebuffer = this->create_framebuffers (this->after.render_pass);
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
        vkDeviceWaitIdle (this->context->get_device ());

        this->destroy_depth_buffers ();
        this->destroy_framebuffers ();
        this->destroy_swapchain ();
        this->destroy_frame_resources ();

        if (this->main.render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass (this->context->get_device (), this->main.render_pass, nullptr);
            this->main.render_pass = VK_NULL_HANDLE;
        }
        if (this->after.render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass (this->context->get_device (), this->after.render_pass, nullptr);
            this->after.render_pass = VK_NULL_HANDLE;
        }
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

VkRenderPass PresentationContext::create_render_pass (VkAttachmentLoadOp load_op) {
    VkAttachmentDescription color_attachment {};
    color_attachment.format = this->swapchain.GetFormat ();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = load_op;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    assert (this->depth_format != VK_FORMAT_UNDEFINED);

    VkAttachmentDescription depth_attachment {};
    depth_attachment.format = this->depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = load_op;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_attachment_ref {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    std::array <VkSubpassDependency, 2> dependencies;

    dependencies [0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies [0].dstSubpass = 0;
    dependencies [0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies [0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies [0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies [0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies [0].dependencyFlags = 0;

    dependencies [1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies [1].dstSubpass = 0;
    dependencies [1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies [1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies [1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies [1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies [1].dependencyFlags = 0;

    std::array <VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
    render_pass_info.pAttachments = attachments.data ();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = static_cast <uint32_t> (dependencies.size ());
    render_pass_info.pDependencies = dependencies.data ();

    VkRenderPass created_render_pass;
    VK_CHECK_RESULT (vkCreateRenderPass (this->context->get_device (), &render_pass_info, nullptr, &created_render_pass));
    return created_render_pass;
}

void PresentationContext::create_depth_buffers () {
    assert (this->depth_format != VK_FORMAT_UNDEFINED);

    const uint32_t width = this->swapchain.GetExtent ().width;
    const uint32_t height = this->swapchain.GetExtent ().height;
    assert (width > 0 && height > 0);

    bool recreated = false;
    if (this->depth_buffers.size ()) {
        recreated = true;
        this->destroy_depth_buffers ();
    }

    this->depth_buffers.resize (this->frames_in_swapchain);

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    VkImageCreateInfo create_info = vk_utils::defaultImageCreateInfo (width, height, this->depth_format, usage, 1);

    for (size_t i = 0; i < this->frames_in_swapchain; ++i) {
        this->depth_buffers [i].format = this->depth_format;

        VK_CHECK_RESULT (vkCreateImage (this->context->get_device (), &create_info, nullptr, &this->depth_buffers [i].image));
        vkGetImageMemoryRequirements (this->context->get_device (), this->depth_buffers [i].image, &this->depth_buffers [i].memReq);

        VkMemoryAllocateInfo mem_alloc {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.allocationSize = this->depth_buffers [i].memReq.size;
        mem_alloc.memoryTypeIndex = vk_utils::findMemoryType (this->depth_buffers [i].memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, this->context->get_physical_device ());
        VK_CHECK_RESULT (vkAllocateMemory (this->context->get_device (), &mem_alloc, nullptr, &this->depth_buffers [i].mem));
        VK_CHECK_RESULT (vkBindImageMemory (this->context->get_device (), this->depth_buffers [i].image, this->depth_buffers [i].mem, 0));

        VkImageViewCreateInfo depth_attachment = vk_utils::defaultImageViewCreateInfo (this->depth_buffers [i].image, this->depth_format, 1, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK_RESULT (vkCreateImageView (this->context->get_device (), &depth_attachment, nullptr, &this->depth_buffers [i].view));
    }

    LOG_INFO ("[PresentationContext] {} {} depth buffers with size ({}, {}).", (recreated) ? "Recreated" : "Created", this->frames_in_swapchain, width, height);
}

std::vector <VkFramebuffer> PresentationContext::create_framebuffers (VkRenderPass a_render_pass) {
    std::array <VkImageView, 2> attachments;

    VkFramebufferCreateInfo framebuffer_info = {};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = a_render_pass;
    framebuffer_info.width = this->swapchain.GetExtent ().width;
    framebuffer_info.height = this->swapchain.GetExtent ().height;
    framebuffer_info.layers = 1;
    framebuffer_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
    framebuffer_info.pAttachments = attachments.data ();

    uint32_t framebuffers_count = this->swapchain.GetImageCount ();
    std::vector <VkFramebuffer> framebuffers (framebuffers_count);

    for (uint32_t i = 0; i < framebuffers_count; i++) {
        assert (this->swapchain.GetAttachment (i).view != VK_NULL_HANDLE);
        assert (this->depth_buffers [i].view != VK_NULL_HANDLE);
        attachments [0] = this->swapchain.GetAttachment (i).view;
        attachments [1] = this->depth_buffers [i].view;
        VK_CHECK_RESULT (vkCreateFramebuffer (this->context->get_device (), &framebuffer_info, nullptr, &framebuffers [i]));
    }

    return framebuffers;
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
    this->create_depth_buffers ();
    this->destroy_framebuffers ();
    this->main.framebuffer = this->create_framebuffers (this->main.render_pass);
    this->after.framebuffer = this->create_framebuffers (this->after.render_pass);
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

void PresentationContext::destroy_depth_buffers () {
    for (size_t i = 0; i < this->depth_buffers.size (); ++i) {
        vk_utils::deleteImg (this->context->get_device (), &this->depth_buffers [i]);
    }
    this->depth_buffers.clear ();
}

void PresentationContext::destroy_framebuffers () {
    for (auto framebuffer : this->main.framebuffer) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->context->get_device (), framebuffer, nullptr);
    }
    for (auto framebuffer : this->after.framebuffer) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->context->get_device (), framebuffer, nullptr);
    }

    this->main.framebuffer.clear ();
    this->after.framebuffer.clear ();
}

void PresentationContext::destroy_frame_resources () {
    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        vkDestroySemaphore (this->context->get_device (), this->frame_resources [i].wait_before_color_attachment_output, nullptr);
        vkDestroySemaphore (this->context->get_device (), this->frame_resources [i].wait_before_depth_copy, nullptr);
        vkDestroyFence (this->context->get_device (), this->frame_resources [i].cpu_wait_next_frame, nullptr);
    }
    this->frame_resources.clear ();
}

}
