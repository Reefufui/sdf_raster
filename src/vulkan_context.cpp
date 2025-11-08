#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

#include "vulkan_context.hpp"

namespace sdf_raster {

VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_message_callback (
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *) {
    std::cerr << "Validation Layer ";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << "ERROR: ";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "WARNING: ";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        std::cerr << "INFO: ";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        std::cerr << "VERBOSE: ";
    }

    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        std::cerr << "GENERAL ";
    }
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        std::cerr << "VALIDATION ";
    }
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        std::cerr << "PERFORMANCE ";
    }
    std::cerr << ": " << pCallbackData->pMessage << std::endl;
    return VK_FALSE;
}

void VulkanContext::init (int a_width, int a_height) {
    VK_CHECK_RESULT (volkInitialize ());
    this->create_instance ();
    this->setup_debug_utils_messenger ();
    this->physical_device = vk_utils::findPhysicalDevice (this->get_instance (), true, 0, {});
    this->create_device ();
    this->create_command_pools ();
    this->get_device_queues ();
    this->copy_helper = std::make_shared <vk_utils::PingPongCopyHelper> (this->get_physical_device ()
            , this->get_device ()
            , this->get_transfer_queue ()
            , this->device_queue_ids.transfer
            , 64 * 1024 * 1024); // staging buffer size

    if (this->window) {
        VK_CHECK_RESULT (glfwCreateWindowSurface (this->get_instance (), this->window, nullptr, &this->surface));
    } else {
        std::cout << "[init] launched in headless mode." << std::endl;
    }

    uint32_t width = a_width;
    uint32_t height = a_height;
    this->present_queue = this->swapchain.CreateSwapChain (this->get_physical_device ()
            , this->get_device ()
            , this->surface
            , width
            , height
            , this->max_frames_in_flight
            , true);

    this->create_render_pass ();
    this->swapchain_framebuffers = vk_utils::createFrameBuffers (this->get_device (), this->swapchain, this->render_pass);
    this->create_frame_resources ();

    this->initialized = true;
}

void VulkanContext::init (GLFWwindow* a_window, int a_width, int a_height) {
    this->window = a_window;
    this->init (a_width, a_height);
}

void VulkanContext::create_instance () {
    VkApplicationInfo app_info {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "sdf_raster";
    app_info.applicationVersion = 0;
    app_info.pEngineName = "vk_utils";
    app_info.engineVersion = 0;
    app_info.apiVersion = VK_API_VERSION_1_3;

    bool enable_validation_layers = true;
    std::vector <const char *> instance_layers {};
    std::vector <const char *> instance_extensions {};

    if (this->window) {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions (&glfwExtensionCount);

        for (size_t i = 0; i < glfwExtensionCount; ++i) {
            instance_extensions.push_back (glfwExtensions [i]);
        }
    }

    instance_layers.push_back("VK_LAYER_KHRONOS_validation");

    this->instance = vk_utils::createInstance (enable_validation_layers
            , instance_layers
            , instance_extensions
            , &app_info);

    volkLoadInstance (this->get_instance ());
}

void VulkanContext::setup_debug_utils_messenger () {
    if (vkCreateDebugUtilsMessengerEXT == nullptr) {
        vkCreateDebugUtilsMessengerEXT = reinterpret_cast <PFN_vkCreateDebugUtilsMessengerEXT> (vkGetInstanceProcAddr (
                    this->get_instance (), "vkCreateDebugUtilsMessengerEXT"));
    }
    if (vkCreateDebugUtilsMessengerEXT != nullptr) { 
        VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info {};
	    debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	    debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	    debug_utils_messenger_create_info.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
	    debug_utils_messenger_create_info.pfnUserCallback = debug_utils_message_callback;
	    auto debugEnabled = vkCreateDebugUtilsMessengerEXT (this->get_instance (), &debug_utils_messenger_create_info, nullptr, &debug_utils_messenger);
        if (debugEnabled != VK_SUCCESS) {
            std::runtime_error {"[setup_debug_utils_messenger] vkCreateDebugUtilsMessengerEXT failed"};
        }
    } else {
        std::runtime_error {"[setup_debug_utils_messenger] vkCreateDebugUtilsMessengerEXT not found"};
    }
}

void VulkanContext::create_device () {
    std::vector <const char*> validation_layers {};
    std::vector <const char*> device_extensions {};
    VkPhysicalDeviceFeatures enabled_device_featurues {};

    device_extensions.push_back (VK_EXT_MESH_SHADER_EXTENSION_NAME);
    device_extensions.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures {};
    meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    meshShaderFeatures.pNext = nullptr;

    VkPhysicalDeviceFeatures2 features2 {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &meshShaderFeatures;

    vkGetPhysicalDeviceFeatures2 (this->get_physical_device (), &features2);

    if (!meshShaderFeatures.meshShader) {
        std::runtime_error ("Mesh Shaders are NOT supported.");
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT requestedMeshShaderFeatures {};
    requestedMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    requestedMeshShaderFeatures.pNext = nullptr;
    requestedMeshShaderFeatures.taskShader = VK_TRUE;
    requestedMeshShaderFeatures.meshShader = VK_TRUE;

    this->device = vk_utils::createLogicalDevice (this->get_physical_device ()
            , validation_layers
            , device_extensions
            , enabled_device_featurues
            , this->device_queue_ids
            , VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT
            , &requestedMeshShaderFeatures);

    volkLoadDevice (this->get_device ());                                            
}

void VulkanContext::create_command_pools () {
    this->compute_command_pool_reset = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.compute, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    this->graphics_command_pool_reset = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    this->transfer_command_pool_reset = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.transfer, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    this->compute_command_pool_transistent = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.compute, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    this->graphics_command_pool_transistent = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.graphics, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    this->transfer_command_pool_transistent = vk_utils::createCommandPool (this->get_device (), this->device_queue_ids.transfer, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
}

void VulkanContext::get_device_queues () {
    vkGetDeviceQueue (this->get_device (), this->device_queue_ids.compute, 0, &compute_queue);
    vkGetDeviceQueue (this->get_device (), this->device_queue_ids.graphics, 0, &graphics_queue);
    vkGetDeviceQueue (this->get_device (), this->device_queue_ids.transfer, 0, &transfer_queue);
}

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector <VkSurfaceFormatKHR> formats;
    std::vector <VkPresentModeKHR> present_modes;
};

SwapChainSupportDetails query_swap_chain_support (VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;
    VK_CHECK_RESULT (vkGetPhysicalDeviceSurfaceCapabilitiesKHR (device, surface, &details.capabilities));

    uint32_t format_count;
    VK_CHECK_RESULT (vkGetPhysicalDeviceSurfaceFormatsKHR (device, surface, &format_count, nullptr));
    if (format_count != 0) {
        details.formats.resize (format_count);
        VK_CHECK_RESULT (vkGetPhysicalDeviceSurfaceFormatsKHR (device, surface, &format_count, details.formats.data ()));
    }

    uint32_t present_mode_count;
    VK_CHECK_RESULT (vkGetPhysicalDeviceSurfacePresentModesKHR (device, surface, &present_mode_count, nullptr));
    if (present_mode_count != 0) {
        details.present_modes.resize (present_mode_count);
        VK_CHECK_RESULT (vkGetPhysicalDeviceSurfacePresentModesKHR (device, surface, &present_mode_count, details.present_modes.data()));
    }
    return details;
}

VkSurfaceFormatKHR choose_swap_surface_format (const std::vector<VkSurfaceFormatKHR>& available_formats) {
    for (const auto& available_format : available_formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_format;
        }
    }
    return available_formats [0];
}

VkPresentModeKHR choose_swap_present_mode (const std::vector<VkPresentModeKHR>& available_present_modes) {
    for (const auto& available_present_mode : available_present_modes) {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D choose_swap_extent (const VkSurfaceCapabilitiesKHR& capabilities, int width, int height) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        VkExtent2D actual_extent = {
            static_cast <uint32_t> (width),
            static_cast <uint32_t> (height)
        };
        actual_extent.width = std::max (capabilities.minImageExtent.width, std::min (capabilities.maxImageExtent.width, actual_extent.width));
        actual_extent.height = std::max (capabilities.minImageExtent.height, std::min (capabilities.maxImageExtent.height, actual_extent.height));
        return actual_extent;
    }
}

void VulkanContext::shutdown () {
    if (!this->initialized) {
        std::cerr << "[VulkanContext::shutdown] Warning: Attempted to shut down an uninitialized or already shut down VulkanContext." << std::endl;
        return;
    }

    if (this->get_device() == VK_NULL_HANDLE) {
        std::cerr << "[VulkanContext::shutdown] Warning: Vulkan device was VK_NULL_HANDLE during shutdown. Resources might not have been created." << std::endl;
    } else {
        vkDeviceWaitIdle (this->get_device ());
    }

    this->swapchain.Cleanup ();
    for (auto framebuffer : this->swapchain_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->get_device (), framebuffer, nullptr);
        }
    }
    this->swapchain_framebuffers.clear ();

    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        vkDestroySemaphore (this->get_device (), this->frame_resources [i].ready_to_present, nullptr);
        vkDestroySemaphore (this->get_device (), this->frame_resources [i].ready_to_render, nullptr);
        vkDestroyFence (this->get_device (), this->frame_resources [i].ready_to_record, nullptr);
    }

    if (this->render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass (this->get_device (), this->render_pass, nullptr);
        this->render_pass = VK_NULL_HANDLE;
    }

    if (this->compute_command_pool_reset != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->compute_command_pool_reset, nullptr);
        this->compute_command_pool_reset = VK_NULL_HANDLE;
    }
    if (this->graphics_command_pool_reset != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->graphics_command_pool_reset, nullptr);
        this->graphics_command_pool_reset = VK_NULL_HANDLE;
    }
    if (this->transfer_command_pool_reset != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->transfer_command_pool_reset, nullptr);
        this->transfer_command_pool_reset = VK_NULL_HANDLE;
    }
    if (this->compute_command_pool_transistent != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->compute_command_pool_transistent, nullptr);
        this->compute_command_pool_transistent = VK_NULL_HANDLE;
    }
    if (this->graphics_command_pool_transistent != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->graphics_command_pool_transistent, nullptr);
        this->graphics_command_pool_transistent = VK_NULL_HANDLE;
    }
    if (this->transfer_command_pool_transistent != VK_NULL_HANDLE) {
        vkDestroyCommandPool (this->get_device (), this->transfer_command_pool_transistent, nullptr);
        this->transfer_command_pool_transistent = VK_NULL_HANDLE;
    }

    this->copy_helper.reset ();

    if (this->device != VK_NULL_HANDLE) {
        vkDestroyDevice (this->device, nullptr);
        this->device = VK_NULL_HANDLE;
    }

    if (this->surface != VK_NULL_HANDLE) {
        if (this->get_instance () != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR (this->get_instance(), this->surface, nullptr);
        } else {
            std::cerr << "[VulkanContext::shutdown] Warning: VkInstance was VK_NULL_HANDLE while destroying VkSurfaceKHR." << std::endl;
        }
        this->surface = VK_NULL_HANDLE;
    }

    if (this->debug_utils_messenger != VK_NULL_HANDLE) {
        if (vkDestroyDebugUtilsMessengerEXT != nullptr && this->get_instance () != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT (this->get_instance (), this->debug_utils_messenger, nullptr);
        } else {
            std::cerr << "[VulkanContext::shutdown] Warning: Could not destroy debug messenger (PFN or Instance was NULL)." << std::endl;
        }
        this->debug_utils_messenger = VK_NULL_HANDLE;
    }

    if (this->instance != VK_NULL_HANDLE) {
        vkDestroyInstance (this->instance, nullptr);
        this->instance = VK_NULL_HANDLE;
    }

    this->initialized = false;
    std::cout << "[VulkanContext::shutdown] VulkanContext shut down successfully." << std::endl;
}

void VulkanContext::resize(int a_width, int a_height) {
    if (a_width == 0 || a_height == 0) {
        return;
    }
    vkDeviceWaitIdle (this->get_device ());

    for (auto framebuffer : this->swapchain_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->get_device (), framebuffer, nullptr);
    }
    this->swapchain_framebuffers.clear ();
    this->swapchain.Cleanup ();

    uint32_t width = a_width;
    uint32_t height = a_height;
    this->swapchain.CreateSwapChain (this->get_physical_device ()
                                     , this->get_device ()
                                     , this->surface
                                     , width
                                     , height
                                     , this->max_frames_in_flight
                                     , true);
    this->swapchain_framebuffers = vk_utils::createFrameBuffers (this->get_device (), this->swapchain, this->render_pass);

    this->create_frame_resources ();

    this->current_frame = 0;
}

void VulkanContext::create_render_pass () {
    VkAttachmentDescription color_attachment {};
    color_attachment.format = this->swapchain.GetFormat ();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;

    VkSubpassDependency dependency {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &color_attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;

    VK_CHECK_RESULT (vkCreateRenderPass (this->get_device (), &render_pass_info, nullptr, &this->render_pass));
}

void VulkanContext::create_frame_resources () {
    this->frame_resources.resize (this->max_frames_in_flight);

    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = this->get_graphics_command_pool_reset ();
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    for (size_t i = 0; i < max_frames_in_flight; i++) {
        if (vkCreateSemaphore (this->get_device (), &semaphore_info, nullptr, &this->frame_resources[i].ready_to_render) != VK_SUCCESS ||
            vkCreateSemaphore (this->get_device (), &semaphore_info, nullptr, &this->frame_resources[i].ready_to_present) != VK_SUCCESS ||
            vkCreateFence (this->get_device (), &fence_info, nullptr, &this->frame_resources[i].ready_to_record) != VK_SUCCESS) {
            throw std::runtime_error ("Failed to create semaphores or fences for a frame!");
        }

        VK_CHECK_RESULT (vkAllocateCommandBuffers (this->get_device (), &alloc_info, &this->frame_resources[i].command_buffer));
    }
}

VkCommandBuffer VulkanContext::begin_frame () {
    std::cout << "current frame: " << this->current_frame << std::endl;
    vkWaitForFences (this->get_device (), 1, &this->frame_resources [this->current_frame].ready_to_record, VK_TRUE, UINT64_MAX);

    VkResult result = this->swapchain.AcquireNextImage (this->frame_resources [this->current_frame].ready_to_render, &this->current_image_index);
    std::cout << "current image index: " << this->current_image_index << std::endl;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        int width, height;
        glfwGetFramebufferSize (this->window, &width, &height);
        resize (width, height);
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to acquire swap chain image!");
    }

    vkResetFences (this->get_device (), 1, &this->frame_resources [this->current_frame].ready_to_record);

    vkResetCommandBuffer (this->frame_resources [this->current_frame].command_buffer, 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VK_CHECK_RESULT (vkBeginCommandBuffer (this->frame_resources [this->current_frame].command_buffer, &begin_info));

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->render_pass;
    render_pass_info.framebuffer = this->swapchain_framebuffers [this->current_image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = this->swapchain.GetExtent ();

    VkClearValue clear_color = {{{0.2f, 0.3f, 0.3f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass (this->frame_resources [this->current_frame].command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast <float> (this->swapchain.GetExtent ().width);
    viewport.height = static_cast <float> (this->swapchain.GetExtent ().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport (this->frame_resources [this->current_frame].command_buffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = this->swapchain.GetExtent ();
    vkCmdSetScissor (this->frame_resources [this->current_frame].command_buffer, 0, 1, &scissor);

    return this->frame_resources [this->current_frame].command_buffer;
}

void VulkanContext::end_frame (VkCommandBuffer command_buffer) {
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdEndRenderPass (command_buffer);
    VK_CHECK_RESULT (vkEndCommandBuffer (command_buffer));

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores [] = {this->frame_resources [this->current_frame].ready_to_render};
    VkPipelineStageFlags wait_stages [] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VkSemaphore signal_semaphores[] = {this->frame_resources [this->current_frame].ready_to_present};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, this->frame_resources [this->current_frame].ready_to_record));

    VkResult result = this->swapchain.QueuePresent (this->present_queue, this->current_image_index, this->frame_resources [this->current_frame].ready_to_present);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        int width, height;
        glfwGetFramebufferSize (this->window, &width, &height);
        resize (width, height);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to present swap chain image!");
    }

    this->current_frame = (this->current_frame + 1) % this->max_frames_in_flight;
}

}

