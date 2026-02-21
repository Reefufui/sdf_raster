#include <algorithm>
#include <array>
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

void VulkanContext::init (int a_width, int a_height, bool a_mesh_shader_support) {
    VK_CHECK_RESULT (volkInitialize ());
    this->create_instance ();
    this->setup_debug_utils_messenger ();
    this->physical_device = vk_utils::findPhysicalDevice (this->get_instance (), true, 0, {});
    this->create_device (a_mesh_shader_support);
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

    this->create_depth_resources ();
    this->create_render_pass ();
    this->create_render_pass_after ();
    this->create_framebuffers ();
    this->create_frame_resources ();

    this->initialized = true;
}

void VulkanContext::init (GLFWwindow* a_window, int a_width, int a_height, bool a_mesh_shader_support) {
    this->window = a_window;
    this->init (a_width, a_height, a_mesh_shader_support);
}

void VulkanContext::create_instance () {
    VkApplicationInfo app_info {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "sdf_raster";
    app_info.applicationVersion = 0;
    app_info.pEngineName = "vk_utils";
    app_info.engineVersion = 0;
    app_info.apiVersion = VK_API_VERSION_1_4;

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

#ifdef __APPLE__
    instance_extensions.push_back (VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    instance_extensions.push_back (VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    instance_layers.push_back("VK_LAYER_KHRONOS_validation");

    VkInstanceCreateFlagBits flags {};
#ifdef __APPLE__
    flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    void* pNext = nullptr;
#ifdef __APPLE__
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info {};
    if (enable_validation_layers) {
        debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_messenger_create_info.pfnUserCallback = debug_utils_message_callback;
        pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_messenger_create_info;
    }
#endif

    this->instance = vk_utils::createInstance (enable_validation_layers
            , instance_layers
            , instance_extensions
            , &app_info
            , flags
            , pNext);

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

void VulkanContext::dump_mesh_shader_properties () const {
    std::cout << "\n--- VkPhysicalDeviceMeshShaderPropertiesEXT ---" << std::endl;
    std::cout << "  maxTaskWorkGroupTotalCount: " << mesh_shader_properties.maxTaskWorkGroupTotalCount << std::endl;
    std::cout << "  maxTaskWorkGroupCount: ["
              << mesh_shader_properties.maxTaskWorkGroupCount[0] << ", "
              << mesh_shader_properties.maxTaskWorkGroupCount[1] << ", "
              << mesh_shader_properties.maxTaskWorkGroupCount[2] << "]" << std::endl;
    std::cout << "  maxTaskWorkGroupInvocations: " << mesh_shader_properties.maxTaskWorkGroupInvocations << std::endl;
    std::cout << "  maxTaskWorkGroupSize: ["
              << mesh_shader_properties.maxTaskWorkGroupSize[0] << ", "
              << mesh_shader_properties.maxTaskWorkGroupSize[1] << ", "
              << mesh_shader_properties.maxTaskWorkGroupSize[2] << "]" << std::endl;
    std::cout << "  maxTaskPayloadSize: " << mesh_shader_properties.maxTaskPayloadSize << std::endl;
    std::cout << "  maxTaskSharedMemorySize: " << mesh_shader_properties.maxTaskSharedMemorySize << std::endl;
    std::cout << "  maxTaskPayloadAndSharedMemorySize: " << mesh_shader_properties.maxTaskPayloadAndSharedMemorySize << std::endl;
    std::cout << "  maxMeshWorkGroupTotalCount: " << mesh_shader_properties.maxMeshWorkGroupTotalCount << std::endl;
    std::cout << "  maxMeshWorkGroupCount: ["
              << mesh_shader_properties.maxMeshWorkGroupCount[0] << ", "
              << mesh_shader_properties.maxMeshWorkGroupCount[1] << ", "
              << mesh_shader_properties.maxMeshWorkGroupCount[2] << "]" << std::endl;
    std::cout << "  maxMeshWorkGroupInvocations: " << mesh_shader_properties.maxMeshWorkGroupInvocations << std::endl;
    std::cout << "  maxMeshWorkGroupSize: ["
              << mesh_shader_properties.maxMeshWorkGroupSize[0] << ", "
              << mesh_shader_properties.maxMeshWorkGroupSize[1] << ", "
              << mesh_shader_properties.maxMeshWorkGroupSize[2] << "]" << std::endl;
    std::cout << "  maxMeshSharedMemorySize: " << mesh_shader_properties.maxMeshSharedMemorySize << std::endl;
    std::cout << "  maxMeshPayloadAndSharedMemorySize: " << mesh_shader_properties.maxMeshPayloadAndSharedMemorySize << std::endl;
    std::cout << "  maxMeshOutputMemorySize: " << mesh_shader_properties.maxMeshOutputMemorySize << std::endl;
    std::cout << "  maxMeshPayloadAndOutputMemorySize: " << mesh_shader_properties.maxMeshPayloadAndOutputMemorySize << std::endl;
    std::cout << "  maxMeshOutputComponents: " << mesh_shader_properties.maxMeshOutputComponents << std::endl;
    std::cout << "  maxMeshOutputVertices: " << mesh_shader_properties.maxMeshOutputVertices << std::endl;
    std::cout << "  maxMeshOutputPrimitives: " << mesh_shader_properties.maxMeshOutputPrimitives << std::endl;
    std::cout << "  maxMeshOutputLayers: " << mesh_shader_properties.maxMeshOutputLayers << std::endl;
    std::cout << "  maxMeshMultiviewViewCount: " << mesh_shader_properties.maxMeshMultiviewViewCount << std::endl;
    std::cout << "  meshOutputPerVertexGranularity: " << mesh_shader_properties.meshOutputPerVertexGranularity << std::endl;
    std::cout << "  meshOutputPerPrimitiveGranularity: " << mesh_shader_properties.meshOutputPerPrimitiveGranularity << std::endl;
    std::cout << "  maxPreferredTaskWorkGroupInvocations: " << mesh_shader_properties.maxPreferredTaskWorkGroupInvocations << std::endl;
    std::cout << "  maxPreferredMeshWorkGroupInvocations: " << mesh_shader_properties.maxPreferredMeshWorkGroupInvocations << std::endl;
    std::cout << "  prefersLocalInvocationVertexOutput: " << (mesh_shader_properties.prefersLocalInvocationVertexOutput ? "true" : "false") << std::endl;
    std::cout << "  prefersLocalInvocationPrimitiveOutput: " << (mesh_shader_properties.prefersLocalInvocationPrimitiveOutput ? "true" : "false") << std::endl;
    std::cout << "  prefersCompactVertexOutput: " << (mesh_shader_properties.prefersCompactVertexOutput ? "true" : "false") << std::endl;
    std::cout << "  prefersCompactPrimitiveOutput: " << (mesh_shader_properties.prefersCompactPrimitiveOutput ? "true" : "false") << std::endl;
    std::cout << "-------------------------------------------\n" << std::endl;
}

void VulkanContext::create_device (bool a_mesh_shader_support) {
    std::vector <const char*> validation_layers {};
    std::vector <const char*> device_extensions {};
    VkPhysicalDeviceFeatures enabled_device_featurues {};
    // VkPhysicalDeviceFeatures requested_features = {};

    vkGetPhysicalDeviceFeatures (this->get_physical_device (), &enabled_device_featurues);
    if (enabled_device_featurues.wideLines) {
        std::cout << "Physical device supports wideLines. Enabling it." << std::endl;
        // requested_features.wideLines = VK_TRUE; // TODO:
    } else {
        std::cerr << "Physical device does NOT support wideLines. Defaulting to lineWidth = 1.0." << std::endl;
    }

    device_extensions.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef __APPLE__
    device_extensions.push_back (VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    void* pNextFeatures {nullptr};
    VkPhysicalDeviceMeshShaderFeaturesEXT requestedMeshShaderFeatures {};
    if (a_mesh_shader_support) {
        device_extensions.push_back (VK_EXT_MESH_SHADER_EXTENSION_NAME);

        VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures {};
        meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        meshShaderFeatures.pNext = nullptr;

        VkPhysicalDeviceFeatures2 features2 {};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &meshShaderFeatures;

        vkGetPhysicalDeviceFeatures2 (this->get_physical_device (), &features2);

        VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties {};
        meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
        meshShaderProperties.pNext = nullptr;

        VkPhysicalDeviceProperties2 properties2 {};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties2.pNext = &meshShaderProperties;

        vkGetPhysicalDeviceProperties2 (this->get_physical_device (), &properties2);

        if (!meshShaderFeatures.meshShader) {
            throw std::runtime_error ("Mesh Shaders are NOT supported.");
        }

        this->mesh_shader_properties = meshShaderProperties;

        requestedMeshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        requestedMeshShaderFeatures.pNext = nullptr;
        requestedMeshShaderFeatures.taskShader = VK_TRUE;
        requestedMeshShaderFeatures.meshShader = VK_TRUE;

        pNextFeatures = &requestedMeshShaderFeatures;
    }

    this->device = vk_utils::createLogicalDevice (this->get_physical_device ()
            , validation_layers
            , device_extensions
            , enabled_device_featurues
            , this->device_queue_ids
            , VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT
            , pNextFeatures);

    volkLoadDevice (this->get_device ());                                            

    if (a_mesh_shader_support) {
        this->dump_mesh_shader_properties ();
    }
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

    this->destroy_depth_resources ();

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
    if (this->render_pass_after != VK_NULL_HANDLE) {
        vkDestroyRenderPass (this->get_device (), this->render_pass_after, nullptr);
        this->render_pass_after = VK_NULL_HANDLE;
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

    this->destroy_depth_resources ();

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
    this->create_depth_resources ();
    this->create_framebuffers ();
    this->create_frame_resources ();

    this->current_frame = 0;
    this->current_image_index = 0;
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

    VkAttachmentDescription depth_attachment {};
    depth_attachment.format = this->depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_attachment_ref {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    std::array <VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dependencyFlags = 0;

    std::array <VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
    render_pass_info.pAttachments = attachments.data ();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = static_cast <uint32_t> (dependencies.size ());
    render_pass_info.pDependencies = dependencies.data ();

    VK_CHECK_RESULT (vkCreateRenderPass (this->get_device (), &render_pass_info, nullptr, &this->render_pass));
}

void VulkanContext::create_render_pass_after () {
    VkAttachmentDescription color_attachment {};
    color_attachment.format = this->swapchain.GetFormat ();
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_NONE;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference color_attachment_ref {};
    color_attachment_ref.attachment = 0;
    color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depth_attachment {};
    depth_attachment.format = this->depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_NONE;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depth_attachment_ref {};
    depth_attachment_ref.attachment = 1;
    depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment_ref;
    subpass.pDepthStencilAttachment = &depth_attachment_ref;

    std::array <VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = 0;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dependencyFlags = 0;

    std::array <VkAttachmentDescription, 2> attachments = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast <uint32_t> (attachments.size ());
    render_pass_info.pAttachments = attachments.data ();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = static_cast <uint32_t> (dependencies.size ());
    render_pass_info.pDependencies = dependencies.data ();

    VK_CHECK_RESULT (vkCreateRenderPass (this->get_device (), &render_pass_info, nullptr, &this->render_pass_after));
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
    vkWaitForFences (this->get_device (), 1, &this->frame_resources [this->current_frame].ready_to_record, VK_TRUE, UINT64_MAX);

    VkResult result = this->swapchain.AcquireNextImage (this->frame_resources [this->current_frame].ready_to_render, &this->current_image_index);

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

    return this->frame_resources [this->current_frame].command_buffer;
}

void VulkanContext::end_frame (VkCommandBuffer command_buffer) {
    if (command_buffer == VK_NULL_HANDLE) {
        return;
    }

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

void VulkanContext::create_framebuffers () {
    this->swapchain_framebuffers.resize (this->swapchain.GetImageCount ());

    for (uint32_t i = 0; i < swapchain_framebuffers.size (); i++) {
        std::vector <VkImageView> attachments;
        attachments.push_back (this->swapchain.GetAttachment (i).view);
        attachments.push_back (this->depth_textures [i].view);

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = this->render_pass;
        framebufferInfo.attachmentCount = static_cast <uint32_t> (attachments.size ());
        framebufferInfo.pAttachments = attachments.data ();
        framebufferInfo.width = this->swapchain.GetExtent ().width;
        framebufferInfo.height = this->swapchain.GetExtent ().height;
        framebufferInfo.layers = 1;

        VK_CHECK_RESULT (vkCreateFramebuffer (this->device, &framebufferInfo, nullptr, &this->swapchain_framebuffers [i]));
    }
}

void VulkanContext::create_depth_resources () {
    if (!vk_utils::getSupportedDepthFormat (this->get_physical_device (), {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}, &this->depth_format)) {
        throw std::runtime_error ("create_render_pass: couldn't find supported depth format");
    }

    VkExtent2D swapChainExtent = this->swapchain.GetExtent ();

    this->depth_textures.resize (max_frames_in_flight);

    VkCommandBuffer cmd = vk_utils::createCommandBuffer (this->device, this->transfer_command_pool_transistent);
    VkCommandBufferBeginInfo cmd_buff_info = {};
    cmd_buff_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT (vkBeginCommandBuffer (cmd, &cmd_buff_info));

    for (uint32_t i = 0; i < max_frames_in_flight; ++i) {
        this->depth_textures [i] = vk_utils::createDepthTexture (this->device, this->physical_device
            , swapChainExtent.width, swapChainExtent.height, this->depth_format, true);
    }

    vkEndCommandBuffer (cmd);
    vk_utils::executeCommandBufferNow (cmd, this->transfer_queue, this->device);

    this->depth_sampler = vk_utils::createSampler (this->device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                          VK_BORDER_COLOR_INT_OPAQUE_BLACK, 1);
}

void VulkanContext::destroy_depth_resources () {
    for (uint32_t i = 0; i < max_frames_in_flight; ++i) {
        vk_utils::deleteImg (this->device, &this->depth_textures [i]);
    }
    this->depth_textures.clear ();
    if (this->depth_sampler != VK_NULL_HANDLE) {
        vkDestroySampler (this->device, this->depth_sampler, nullptr);
        this->depth_sampler = VK_NULL_HANDLE;
    }
}

}

