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
    void                                       *pUserData) {
    std::cout << pCallbackData->pMessage << "\n";
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

    this->create_swap_chain (a_width, a_height);
    this->create_image_views ();
    this->create_render_pass ();
    this->create_framebuffers ();
    this->create_main_command_buffers ();
    this->create_sync_objects ();

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
        VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
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

    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext = nullptr
    };

    VkPhysicalDeviceFeatures2 features2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &meshShaderFeatures
    };

    vkGetPhysicalDeviceFeatures2 (this->get_physical_device (), &features2);

    if (!meshShaderFeatures.meshShader) {
        std::runtime_error ("Mesh Shaders are NOT supported.");
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT requestedMeshShaderFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext = nullptr,
        .taskShader = VK_TRUE,
        .meshShader = VK_TRUE
    };

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

void VulkanContext::create_swap_chain (int width, int height) {
    SwapChainSupportDetails swap_chain_support = query_swap_chain_support (this->get_physical_device (), this->surface);

    VkSurfaceFormatKHR surface_format = choose_swap_surface_format (swap_chain_support.formats);
    VkPresentModeKHR present_mode = choose_swap_present_mode (swap_chain_support.present_modes);
    VkExtent2D extent = choose_swap_extent (swap_chain_support.capabilities, width, height);

    uint32_t image_count = swap_chain_support.capabilities.minImageCount + 1;
    if (swap_chain_support.capabilities.maxImageCount > 0 && image_count > swap_chain_support.capabilities.maxImageCount) {
        image_count = swap_chain_support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info {};
    create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.surface = this->surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = surface_format.format;
    create_info.imageColorSpace = surface_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.queueFamilyIndexCount = 0;
    create_info.pQueueFamilyIndices = nullptr;
    create_info.preTransform = swap_chain_support.capabilities.currentTransform;
    create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    create_info.presentMode = present_mode;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK_RESULT (vkCreateSwapchainKHR (this->get_device (), &create_info, nullptr, &this->swap_chain));

    VK_CHECK_RESULT (vkGetSwapchainImagesKHR (this->get_device (), this->swap_chain, &image_count, nullptr));
    this->swap_chain_images.resize (image_count);
    VK_CHECK_RESULT (vkGetSwapchainImagesKHR (this->get_device (), this->swap_chain, &image_count, this->swap_chain_images.data ()));

    this->swap_chain_image_format = surface_format.format;
    this->swap_chain_extent = extent;

    this->copy_helper.reset ();
}

void VulkanContext::shutdown () {
    if (this->get_device () == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle (this->get_device ());

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (this->render_finished_semaphores.size () && this->render_finished_semaphores [i] != VK_NULL_HANDLE) {
            vkDestroySemaphore (this->get_device (), this->render_finished_semaphores [i], nullptr);
        }
        if (this->image_available_semaphores_acquire.size () && this->image_available_semaphores_acquire[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore (this->get_device (), this->image_available_semaphores_acquire [i], nullptr);
        }
        if (this->in_flight_fences.size () && this->in_flight_fences [i] != VK_NULL_HANDLE) {
            vkDestroyFence (this->get_device (), this->in_flight_fences [i], nullptr);
        }
    }
    this->render_finished_semaphores.clear ();
    this->image_available_semaphores_acquire.clear ();
    this->in_flight_fences.clear ();

    for (auto framebuffer : this->swap_chain_framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->get_device (), framebuffer, nullptr);
    }
    this->swap_chain_framebuffers. clear();

    if (this->render_pass != VK_NULL_HANDLE) vkDestroyRenderPass (this->get_device (), this->render_pass, nullptr);
    this->render_pass = VK_NULL_HANDLE;

    for (auto imageView : this->swap_chain_image_views) {
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(this->get_device (), imageView, nullptr);
    }
    this->swap_chain_image_views.clear ();

    if (this->swap_chain != VK_NULL_HANDLE) vkDestroySwapchainKHR (this->get_device (), this->swap_chain, nullptr);
    this->swap_chain = VK_NULL_HANDLE;

    if (this->surface != VK_NULL_HANDLE) vkDestroySurfaceKHR (this->get_instance (), this->surface, nullptr);
    this->surface = VK_NULL_HANDLE;
}

void VulkanContext::resize(int a_width, int a_height) {
    if (a_width == 0 || a_height == 0) {
        return;
    }
    vkDeviceWaitIdle(this->get_device ());

    for (auto framebuffer : this->swap_chain_framebuffers) {
        vkDestroyFramebuffer(this->get_device (), framebuffer, nullptr);
    }
    this->swap_chain_framebuffers.clear();
    for (auto imageView : this->swap_chain_image_views) {
        vkDestroyImageView(this->get_device (), imageView, nullptr);
    }
    this->swap_chain_image_views.clear();

    vkDestroySwapchainKHR(this->get_device (), this->swap_chain, nullptr);
    this->swap_chain = VK_NULL_HANDLE;

    create_swap_chain (a_width, a_height);
    create_image_views ();
    create_framebuffers ();

    uint32_t swapchain_image_count = this->swap_chain_images.size();
    this->render_finished_semaphores.resize(swapchain_image_count);
    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->get_device (), &semaphore_info, nullptr, &this->render_finished_semaphores [i]));
    }

    this->create_main_command_buffers ();
}

void VulkanContext::create_image_views () {
    this->swap_chain_image_views.resize (this->swap_chain_images.size ());
    for (size_t i = 0; i < this->swap_chain_images.size (); i++) {
        VkImageViewCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        create_info.image = this->swap_chain_images [i];
        create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        create_info.format = this->swap_chain_image_format;
        create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        create_info.subresourceRange.baseMipLevel = 0;
        create_info.subresourceRange.levelCount = 1;
        create_info.subresourceRange.baseArrayLayer = 0;
        create_info.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT (vkCreateImageView (this->get_device (), &create_info, nullptr, &this->swap_chain_image_views [i]));
    }
}

void VulkanContext::create_render_pass () {
    VkAttachmentDescription color_attachment {};
    color_attachment.format = this->swap_chain_image_format;
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

void VulkanContext::create_framebuffers () {
    this->swap_chain_framebuffers.resize (this->swap_chain_image_views.size ());
    for (size_t i = 0; i < this->swap_chain_image_views.size (); i++) {
        VkImageView attachments [] = {
            this->swap_chain_image_views [i]
        };

        VkFramebufferCreateInfo framebuffer_info {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = this->render_pass;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = this->swap_chain_extent.width;
        framebuffer_info.height = this->swap_chain_extent.height;
        framebuffer_info.layers = 1;

        VK_CHECK_RESULT (vkCreateFramebuffer (this->get_device (), &framebuffer_info, nullptr, &this->swap_chain_framebuffers [i]));
    }
}

void VulkanContext::create_main_command_buffers () {
    this->command_buffers.resize (this->swap_chain_framebuffers.size ());

    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = this->graphics_command_pool_reset;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = (uint32_t) this->command_buffers.size ();

    VK_CHECK_RESULT (vkAllocateCommandBuffers (this->get_device (), &alloc_info, this->command_buffers.data ()));
}

void VulkanContext::create_sync_objects () {
    in_flight_fences.resize (MAX_FRAMES_IN_FLIGHT);
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK_RESULT (vkCreateFence (this->get_device (), &fence_info, nullptr, &this->in_flight_fences [i]));
    }

    uint32_t swapchain_image_count = this->swap_chain_images.size ();
    if (!swapchain_image_count) {
        std::logic_error {"[VulkanContext::create_sync_objects] missing swap chain images"};
    }
    render_finished_semaphores.resize (swapchain_image_count);
    VkSemaphoreCreateInfo semaphore_info {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < swapchain_image_count; ++i) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->get_device (), &semaphore_info, nullptr, &this->render_finished_semaphores [i]));
    }

    image_available_semaphores_acquire.resize (MAX_FRAMES_IN_FLIGHT); 
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->get_device (), &semaphore_info, nullptr, &this->image_available_semaphores_acquire [i]));
    }
}

VkCommandBuffer VulkanContext::begin_frame () {
    vkWaitForFences (this->get_device (), 1, &this->in_flight_fences [this->current_frame], VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR (this->get_device (), this->swap_chain, UINT64_MAX, this->image_available_semaphores_acquire [this->current_frame], VK_NULL_HANDLE, &image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        int width, height;
        glfwGetFramebufferSize (this->window, &width, &height);
        resize (width, height);
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to acquire swap chain image!");
    }

    vkResetFences (this->get_device (), 1, &this->in_flight_fences [this->current_frame]);
    vkResetCommandBuffer (this->command_buffers [image_index], 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VK_CHECK_RESULT (vkBeginCommandBuffer (this->command_buffers [image_index], &begin_info));

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->render_pass;
    render_pass_info.framebuffer = this->swap_chain_framebuffers [image_index];
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = this->swap_chain_extent;

    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

    vkCmdBeginRenderPass (this->command_buffers [image_index], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast <float> (this->swap_chain_extent.width);
    viewport.height = static_cast <float> (this->swap_chain_extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport (this->command_buffers [image_index], 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = this->swap_chain_extent;
    vkCmdSetScissor (this->command_buffers [image_index], 0, 1, &scissor);

    return this->command_buffers [image_index];
}

void VulkanContext::end_frame (VkCommandBuffer command_buffer) {
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

    vkCmdEndRenderPass (command_buffer);
    VK_CHECK_RESULT (vkEndCommandBuffer (command_buffer));

    uint32_t image_index = 0;
    for (uint32_t i = 0; i < this->command_buffers.size (); ++i) {
        if (this->command_buffers [i] == command_buffer) {
            image_index = i;
            break;
        }
    }

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore wait_semaphores [] = {this->image_available_semaphores_acquire [this->current_frame]};
    VkPipelineStageFlags wait_stages [] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;

    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;

    VkSemaphore signal_semaphores_for_present [] = {this->render_finished_semaphores [image_index]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores_for_present;

    VK_CHECK_RESULT (vkQueueSubmit (this->get_graphics_queue (), 1, &submit_info, this->in_flight_fences [this->current_frame]));

    VkPresentInfoKHR present_info {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores_for_present;

    VkSwapchainKHR swap_chains [] = {this->swap_chain};
    present_info.swapchainCount = 1;
    present_info.pSwapchains = swap_chains;
    present_info.pImageIndices = &image_index;
    present_info.pResults = nullptr;

    VkResult result = vkQueuePresentKHR (this->get_graphics_queue (), &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        int width, height;
        glfwGetFramebufferSize (this->window, &width, &height);
        resize (width, height);
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to present swap chain image!");
    }

    this->current_frame = (this->current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

}

