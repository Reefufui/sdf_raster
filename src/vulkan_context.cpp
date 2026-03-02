#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

#include "vulkan_context.hpp"
#include "logger.hpp"

namespace sdf_raster {

#ifdef VULKAN_VALIDATION_LAYERS

VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_message_callback (
    VkDebugUtilsMessageSeverityFlagBitsEXT      message_severity,
    VkDebugUtilsMessageTypeFlagsEXT             message_type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *) {
    auto logger = spdlog::get ("VK_LAYER_KHRONOS");
    if (!logger) {
        logger = spdlog::default_logger ();
    }

    std::string type_str;
    if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
        type_str += "GENERAL ";
    }
    if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        type_str += "VALIDATION ";
    }
    if (message_type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
        type_str += "PERFORMANCE ";
    }
    if (!type_str.empty ()) {
        type_str.pop_back ();
    }

    const char* message = callback_data->pMessage;
    if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        logger->error ("[{}] {}", type_str, message);
    } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        logger->warn ("[{}] {}", type_str, message);
    } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        logger->info ("[{}] {}", type_str, message);
    } else if (message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        logger->trace ("[{}] {}", type_str, message);
    }

    return VK_FALSE;
}

#endif

void VulkanContext::init (int a_width, int a_height, bool a_mesh_shader_support) {
    VK_CHECK_RESULT (volkInitialize ());
    this->create_instance ();
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
        LOG_INFO ("[VulkanContext] Launched in headless mode. Skipped window creation.");
    }

    this->create_swapchain (static_cast <uint32_t> (a_width), static_cast <uint32_t> (a_height));

    if (!vk_utils::getSupportedDepthFormat (this->get_physical_device (), {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}, &this->depth_format)) {
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

void VulkanContext::init (GLFWwindow* a_window, int a_width, int a_height, bool a_mesh_shader_support) {
    this->window = a_window;
    this->init (a_width, a_height, a_mesh_shader_support);
}

void VulkanContext::create_instance () {
    VkApplicationInfo app_info {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = APP_NAME;
    app_info.applicationVersion = VK_MAKE_VERSION (APP_VERSION_MAJOR, APP_VERSION_MINOR, APP_VERSION_PATCH);
    app_info.pEngineName = nullptr;
    app_info.engineVersion = 0;
    app_info.apiVersion = VK_API_VERSION_1_4;

    bool enable_validation_layers = false;
    std::vector <const char *> instance_layers {};
    std::vector <const char *> instance_extensions {};

    if (this->window) {
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions (&glfwExtensionCount);

        for (size_t i = 0; i < glfwExtensionCount; ++i) {
            instance_extensions.push_back (glfwExtensions [i]);
        }
    }

#ifdef VULKAN_VALIDATION_LAYERS
    instance_extensions.push_back (VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
#ifdef __APPLE__
    instance_extensions.push_back (VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    instance_extensions.push_back (VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    void* pNext = nullptr;
#ifdef VULKAN_VALIDATION_LAYERS
    enable_validation_layers = true;
    instance_layers.push_back ("VK_LAYER_KHRONOS_validation");

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info {};
    debug_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_messenger_create_info.pfnUserCallback = debug_utils_message_callback;
    pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debug_messenger_create_info;
#endif

    VkInstanceCreateFlagBits flags {};
#ifdef __APPLE__
    flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    this->instance = vk_utils::createInstance (enable_validation_layers
            , instance_layers
            , instance_extensions
            , &app_info
            , flags
            , pNext);

    volkLoadInstance (this->get_instance ());

#ifdef VULKAN_VALIDATION_LAYERS
    this->setup_debug_utils_messenger ();
#endif
}

#ifdef VULKAN_VALIDATION_LAYERS
void VulkanContext::setup_debug_utils_messenger () {
    if (vkCreateDebugUtilsMessengerEXT == nullptr) {
        vkCreateDebugUtilsMessengerEXT = reinterpret_cast <PFN_vkCreateDebugUtilsMessengerEXT> (vkGetInstanceProcAddr (
                    this->get_instance (), "vkCreateDebugUtilsMessengerEXT"));
    }
    if (vkCreateDebugUtilsMessengerEXT != nullptr) { 
        VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info {};
	    debug_utils_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	    debug_utils_messenger_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_utils_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	    debug_utils_messenger_create_info.pfnUserCallback = debug_utils_message_callback;
	    auto debugEnabled = vkCreateDebugUtilsMessengerEXT (this->get_instance (), &debug_utils_messenger_create_info, nullptr, &debug_utils_messenger);
        if (debugEnabled != VK_SUCCESS) {
            throw std::runtime_error {"[setup_debug_utils_messenger] vkCreateDebugUtilsMessengerEXT failed"};
        }
    } else {
        throw std::runtime_error {"[setup_debug_utils_messenger] vkCreateDebugUtilsMessengerEXT not found"};
    }
}
#endif

void VulkanContext::dump_mesh_shader_properties () const {
    LOG_INFO("\n--- VkPhysicalDeviceMeshShaderPropertiesEXT ---");
    LOG_INFO("  maxTaskWorkGroupTotalCount: {}", mesh_shader_properties.maxTaskWorkGroupTotalCount);
    LOG_INFO("  maxTaskWorkGroupCount: [{}, {}, {}]",
             mesh_shader_properties.maxTaskWorkGroupCount[0],
             mesh_shader_properties.maxTaskWorkGroupCount[1],
             mesh_shader_properties.maxTaskWorkGroupCount[2]);
    LOG_INFO("  maxTaskWorkGroupInvocations: {}", mesh_shader_properties.maxTaskWorkGroupInvocations);
    LOG_INFO("  maxTaskWorkGroupSize: [{}, {}, {}]",
             mesh_shader_properties.maxTaskWorkGroupSize[0],
             mesh_shader_properties.maxTaskWorkGroupSize[1],
             mesh_shader_properties.maxTaskWorkGroupSize[2]);
    LOG_INFO("  maxTaskPayloadSize: {}", mesh_shader_properties.maxTaskPayloadSize);
    LOG_INFO("  maxTaskSharedMemorySize: {}", mesh_shader_properties.maxTaskSharedMemorySize);
    LOG_INFO("  maxTaskPayloadAndSharedMemorySize: {}", mesh_shader_properties.maxTaskPayloadAndSharedMemorySize);
    LOG_INFO("  maxMeshWorkGroupTotalCount: {}", mesh_shader_properties.maxMeshWorkGroupTotalCount);
    LOG_INFO("  maxMeshWorkGroupCount: [{}, {}, {}]",
             mesh_shader_properties.maxMeshWorkGroupCount[0],
             mesh_shader_properties.maxMeshWorkGroupCount[1],
             mesh_shader_properties.maxMeshWorkGroupCount[2]);
    LOG_INFO("  maxMeshWorkGroupInvocations: {}", mesh_shader_properties.maxMeshWorkGroupInvocations);
    LOG_INFO("  maxMeshWorkGroupSize: [{}, {}, {}]",
             mesh_shader_properties.maxMeshWorkGroupSize[0],
             mesh_shader_properties.maxMeshWorkGroupSize[1],
             mesh_shader_properties.maxMeshWorkGroupSize[2]);
    LOG_INFO("  maxMeshSharedMemorySize: {}", mesh_shader_properties.maxMeshSharedMemorySize);
    LOG_INFO("  maxMeshPayloadAndSharedMemorySize: {}", mesh_shader_properties.maxMeshPayloadAndSharedMemorySize);
    LOG_INFO("  maxMeshOutputMemorySize: {}", mesh_shader_properties.maxMeshOutputMemorySize);
    LOG_INFO("  maxMeshPayloadAndOutputMemorySize: {}", mesh_shader_properties.maxMeshPayloadAndOutputMemorySize);
    LOG_INFO("  maxMeshOutputComponents: {}", mesh_shader_properties.maxMeshOutputComponents);
    LOG_INFO("  maxMeshOutputVertices: {}", mesh_shader_properties.maxMeshOutputVertices);
    LOG_INFO("  maxMeshOutputPrimitives: {}", mesh_shader_properties.maxMeshOutputPrimitives);
    LOG_INFO("  maxMeshOutputLayers: {}", mesh_shader_properties.maxMeshOutputLayers);
    LOG_INFO("  maxMeshMultiviewViewCount: {}", mesh_shader_properties.maxMeshMultiviewViewCount);
    LOG_INFO("  meshOutputPerVertexGranularity: {}", mesh_shader_properties.meshOutputPerVertexGranularity);
    LOG_INFO("  meshOutputPerPrimitiveGranularity: {}", mesh_shader_properties.meshOutputPerPrimitiveGranularity);
    LOG_INFO("  maxPreferredTaskWorkGroupInvocations: {}", mesh_shader_properties.maxPreferredTaskWorkGroupInvocations);
    LOG_INFO("  maxPreferredMeshWorkGroupInvocations: {}", mesh_shader_properties.maxPreferredMeshWorkGroupInvocations);
    LOG_INFO("  prefersLocalInvocationVertexOutput: {}", mesh_shader_properties.prefersLocalInvocationVertexOutput ? "true" : "false");
    LOG_INFO("  prefersLocalInvocationPrimitiveOutput: {}", mesh_shader_properties.prefersLocalInvocationPrimitiveOutput ? "true" : "false");
    LOG_INFO("  prefersCompactVertexOutput: {}", mesh_shader_properties.prefersCompactVertexOutput ? "true" : "false");
    LOG_INFO("  prefersCompactPrimitiveOutput: {}", mesh_shader_properties.prefersCompactPrimitiveOutput ? "true" : "false");
    LOG_INFO("-------------------------------------------\n");
}

void VulkanContext::create_device (bool a_mesh_shader_support) {
    std::vector <const char*> validation_layers_to_enable {}; // validation layers already enabled on instance level
    std::vector <const char*> device_extensions_to_enable {};

    device_extensions_to_enable.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef __APPLE__
    device_extensions_to_enable.push_back (VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    VkPhysicalDeviceFeatures2 device_features_2 {};
    device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    void* pNext_query_chain = nullptr;
    void* pNext_create_chain = nullptr;

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features_query {};
    timeline_semaphore_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
    timeline_semaphore_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &timeline_semaphore_features_query;

    VkPhysicalDeviceVulkanMemoryModelFeatures vulkan_memory_model_features_query {};
    vulkan_memory_model_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
    vulkan_memory_model_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &vulkan_memory_model_features_query;

    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features_query {};
    buffer_device_address_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    buffer_device_address_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &buffer_device_address_features_query;

    VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout_features_query {};
    scalar_block_layout_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
    scalar_block_layout_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &scalar_block_layout_features_query;

    VkPhysicalDevice8BitStorageFeatures eight_bit_storage_features_query {};
    eight_bit_storage_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES;
    eight_bit_storage_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &eight_bit_storage_features_query;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features_query {};
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties_query {};

    if (a_mesh_shader_support) {
        device_extensions_to_enable.push_back (VK_EXT_MESH_SHADER_EXTENSION_NAME);

        mesh_shader_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        mesh_shader_features_query.pNext = pNext_query_chain;
        pNext_query_chain = &mesh_shader_features_query;

        mesh_shader_properties_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
        mesh_shader_properties_query.pNext = nullptr;
    }

    device_features_2.pNext = pNext_query_chain;

    vkGetPhysicalDeviceFeatures2 (this->get_physical_device (), &device_features_2);

    if (a_mesh_shader_support) {
        VkPhysicalDeviceProperties2 properties_2 {};
        properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties_2.pNext = &mesh_shader_properties_query;
        vkGetPhysicalDeviceProperties2 (this->get_physical_device (), &properties_2);
    }

    if (!device_features_2.features.wideLines) {
        LOG_WARN ("[VulkanContext] Physical device does NOT support wideLines. Defaulting to lineWidth = 1.0.");
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features_enable {};
    if (timeline_semaphore_features_query.timelineSemaphore) {
        timeline_semaphore_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        timeline_semaphore_features_enable.timelineSemaphore = VK_TRUE;
        timeline_semaphore_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &timeline_semaphore_features_enable;
    }

    VkPhysicalDeviceVulkanMemoryModelFeatures vulkan_memory_model_features_enable {};
    if (vulkan_memory_model_features_query.vulkanMemoryModel) {
        vulkan_memory_model_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES;
        vulkan_memory_model_features_enable.vulkanMemoryModel = VK_TRUE;
        vulkan_memory_model_features_enable.vulkanMemoryModelDeviceScope = vulkan_memory_model_features_query.vulkanMemoryModelDeviceScope; // если вы хотите и deviceScope
        vulkan_memory_model_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &vulkan_memory_model_features_enable;
    }

    VkPhysicalDeviceBufferDeviceAddressFeatures buffer_device_address_features_enable {};
    if (buffer_device_address_features_query.bufferDeviceAddress) {
        buffer_device_address_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        buffer_device_address_features_enable.bufferDeviceAddress = VK_TRUE;
        buffer_device_address_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &buffer_device_address_features_enable;
    }

    VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_layout_features_enable {};
    if (scalar_block_layout_features_query.scalarBlockLayout) {
        scalar_block_layout_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        scalar_block_layout_features_enable.scalarBlockLayout = VK_TRUE;
        scalar_block_layout_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &scalar_block_layout_features_enable;
    }

    VkPhysicalDevice8BitStorageFeatures eight_bit_storage_features_enable {};
    if (eight_bit_storage_features_query.storageBuffer8BitAccess) {
        eight_bit_storage_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES;
        eight_bit_storage_features_enable.storageBuffer8BitAccess = VK_TRUE;
        eight_bit_storage_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &eight_bit_storage_features_enable;
    }

    VkPhysicalDeviceMeshShaderFeaturesEXT requested_mesh_shader_features_enable {};
    if (a_mesh_shader_support) {
        if (!mesh_shader_features_query.meshShader) {
            throw std::runtime_error("Mesh Shaders are NOT supported on this physical device.");
        }

        requested_mesh_shader_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        requested_mesh_shader_features_enable.taskShader = VK_TRUE;
        requested_mesh_shader_features_enable.meshShader = VK_TRUE;
        requested_mesh_shader_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &requested_mesh_shader_features_enable;

        this->mesh_shader_properties = mesh_shader_properties_query;
    }

    VkPhysicalDeviceFeatures features_to_enable_in_base_struct = device_features_2.features;
#ifdef __APPLE__
    features_to_enable_in_base_struct.robustBufferAccess = VK_FALSE; // unsupported on Metal
#endif

    this->device = vk_utils::createLogicalDevice (this->get_physical_device ()
        , validation_layers_to_enable
        , device_extensions_to_enable
        , features_to_enable_in_base_struct
        , this->device_queue_ids
        , VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT
        , pNext_create_chain
    );

    volkLoadDevice (this->get_device ());

    if (a_mesh_shader_support) {
        this->dump_mesh_shader_properties();
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

// TODO: use this function
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

// TODO: use this function
VkSurfaceFormatKHR choose_swap_surface_format (const std::vector<VkSurfaceFormatKHR>& available_formats) {
    for (const auto& available_format : available_formats) {
        if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return available_format;
        }
    }
    return available_formats [0];
}

// TODO: use this function
VkPresentModeKHR choose_swap_present_mode (const std::vector<VkPresentModeKHR>& available_present_modes) {
    for (const auto& available_present_mode : available_present_modes) {
        if (available_present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return available_present_mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

// TODO: use this function
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
        LOG_WARN ("[VulkanContext] Attempted to shutdown an uninitialized or already shut down vulkan context.");
        return;
    }

    if (this->get_device () == VK_NULL_HANDLE) {
        LOG_WARN ("[VulkanContext] Vulkan device was VK_NULL_HANDLE during shutdown. Resources might not have been created.");
    } else {
        LOG_INFO ("[VulkanContext] Waiting for the GPU to go idle to shutdown application.");
        vkDeviceWaitIdle (this->get_device ());

        this->destroy_depth_buffers ();
        this->destroy_framebuffers ();
        this->destroy_swapchain ();
        this->destroy_frame_resources ();

        auto destroy_render_pass_resources = [&](RenderPassResources& r) {
            if (r.render_pass != VK_NULL_HANDLE) {
                vkDestroyRenderPass (this->device, r.render_pass, nullptr);
                r.render_pass = VK_NULL_HANDLE;
            }
        };
        destroy_render_pass_resources (main);
        destroy_render_pass_resources (after);

        auto destroy_command_pool = [&](VkCommandPool& command_pool) {
            if (command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool (this->device, command_pool, nullptr);
                command_pool = VK_NULL_HANDLE;
            }
        };
        destroy_command_pool (this->compute_command_pool_reset);
        destroy_command_pool (this->compute_command_pool_reset);
        destroy_command_pool (this->compute_command_pool_transistent);
        destroy_command_pool (this->graphics_command_pool_reset);
        destroy_command_pool (this->graphics_command_pool_transistent);
        destroy_command_pool (this->transfer_command_pool_reset);
        destroy_command_pool (this->transfer_command_pool_transistent);

        this->copy_helper.reset ();

        if (this->device != VK_NULL_HANDLE) {
            vkDestroyDevice (this->device, nullptr);
            this->device = VK_NULL_HANDLE;
        }
    }

    if (this->surface != VK_NULL_HANDLE) {
        if (this->get_instance () != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR (this->get_instance (), this->surface, nullptr);
        } else {
            LOG_ERROR ("[VulkanContext] VkInstance was VK_NULL_HANDLE while destroying VkSurfaceKHR.");
        }
        this->surface = VK_NULL_HANDLE;
    }

#ifdef VULKAN_VALIDATION_LAYERS
    if (this->debug_utils_messenger != VK_NULL_HANDLE) {
        if (vkDestroyDebugUtilsMessengerEXT != nullptr && this->get_instance () != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT (this->get_instance (), this->debug_utils_messenger, nullptr);
        } else {
            LOG_ERROR ("[VulkanContext] Could not destroy debug messenger (PFN or Instance was NULL).");
        }
        this->debug_utils_messenger = VK_NULL_HANDLE;
    }
#endif

    if (this->instance != VK_NULL_HANDLE) {
        vkDestroyInstance (this->instance, nullptr);
        this->instance = VK_NULL_HANDLE;
    }

    this->initialized = false;
    LOG_INFO ("[VulkanContext] Vulkan instance destroyed successfully.");
}

void VulkanContext::resize () {
    this->framebuffer_resized = false;

    int width, height;
    glfwGetFramebufferSize (this->window, &width, &height);
    while (width == 0 || height == 0) {
        glfwWaitEvents ();
        glfwGetFramebufferSize (this->window, &width, &height);
    }

    const auto extent = this->swapchain.GetExtent ();
    LOG_INFO ("[VulkanContext] Waiting device: size ({}, {}) is outdated. New window framebuffer size is ({}, {}).", extent.width, extent.height, width, height);
    vkDeviceWaitIdle (this->get_device ());

    this->create_swapchain (static_cast <uint32_t> (width), static_cast <uint32_t> (height));
    this->create_depth_buffers ();
    this->destroy_framebuffers ();
    this->main.framebuffer = this->create_framebuffers (this->main.render_pass);
    this->after.framebuffer = this->create_framebuffers (this->after.render_pass);
    this->create_frame_resources ();

    for (const auto& callback : resizable_callbacks) {
        callback ();
    }
}

void VulkanContext::create_swapchain (uint32_t width, uint32_t height) {
    this->destroy_swapchain ();

    this->present_queue = this->swapchain.CreateSwapChain (this->get_physical_device ()
            , this->get_device ()
            , this->surface
            , width
            , height
            , this->frames_in_swapchain
            , true); // TODO: set in config

    this->frames_in_swapchain = this->swapchain.GetImageCount ();

    this->gpu_ready_to_present.resize (this->frames_in_swapchain);

    for (size_t i = 0; i < this->gpu_ready_to_present.size (); i++) {
        VkSemaphoreCreateInfo semaphoreInfo {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        vkCreateSemaphore (device, &semaphoreInfo, nullptr, &this->gpu_ready_to_present [i]);
    }

    this->acquired_image_index = std::numeric_limits <uint32_t>::max ();

    const auto extent = this->swapchain.GetExtent ();
    LOG_INFO ("[VulkanContext] Created {} swapchain images with size ({}, {}).", this->frames_in_swapchain, extent.width, extent.height);
}

VkRenderPass VulkanContext::create_render_pass (VkAttachmentLoadOp load_op) {
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
    VK_CHECK_RESULT (vkCreateRenderPass (this->get_device (), &render_pass_info, nullptr, &created_render_pass));
    return created_render_pass;
}

void VulkanContext::create_frame_resources () {
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
    alloc_info.commandPool = this->get_graphics_command_pool_reset ();
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

    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        VK_CHECK_RESULT (vkCreateSemaphore (this->device, &semaphore_info, nullptr, &this->frame_resources [i].wait_before_color_attachment_output));
        VK_CHECK_RESULT (vkCreateSemaphore (this->device, &semaphore_info, nullptr, &this->frame_resources [i].wait_before_depth_copy));
        VK_CHECK_RESULT (vkCreateFence (this->get_device (), &fence_info, nullptr, &this->frame_resources [i].cpu_wait_next_frame));
        VK_CHECK_RESULT (vkAllocateCommandBuffers (this->get_device (), &alloc_info, &this->frame_resources [i].command_buffer));

        submit_info.pSignalSemaphores = &this->frame_resources [i].wait_before_depth_copy;
        VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, VK_NULL_HANDLE));
    }
}

VkCommandBuffer VulkanContext::begin_frame (uint32_t frame_idx) {
    vkWaitForFences (this->device, 1, &this->frame_resources [frame_idx].cpu_wait_next_frame, VK_TRUE, UINT64_MAX);
    vkResetFences (this->device, 1, &this->frame_resources [frame_idx].cpu_wait_next_frame);

    VkResult result = this->swapchain.AcquireNextImage (this->frame_resources [frame_idx].wait_before_color_attachment_output, &this->acquired_image_index);
    LOG_TRACE ("in-flight frame: {}, swapchain image: {}", frame_idx, this->acquired_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || this->framebuffer_resized) {
        LOG_INFO ("RESIZING FROM begin_frame");
        this->resize ();
        return VK_NULL_HANDLE;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error ("failed to acquire swap chain image!");
    }

    vkResetCommandBuffer (this->frame_resources [frame_idx].command_buffer, 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VK_CHECK_RESULT (vkBeginCommandBuffer (this->frame_resources [frame_idx].command_buffer, &begin_info));

    return this->frame_resources [frame_idx].command_buffer;
}

void VulkanContext::end_frame (VkCommandBuffer command_buffer, uint32_t frame_idx) {
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

    VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, this->frame_resources [frame_idx].cpu_wait_next_frame));

    VkResult result = this->swapchain.QueuePresent (this->present_queue, this->acquired_image_index, this->gpu_ready_to_present [this->acquired_image_index]);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || this->framebuffer_resized) {
        LOG_INFO ("RESIZING FROM end_frame");
        this->resize ();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error ("failed to present swap chain image!");
    }
}

std::vector <VkFramebuffer> VulkanContext::create_framebuffers (VkRenderPass a_render_pass) {
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
        VK_CHECK_RESULT (vkCreateFramebuffer (this->device, &framebuffer_info, nullptr, &framebuffers [i]));
    }

    return framebuffers;
}

void VulkanContext::create_depth_buffers () {
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

        VK_CHECK_RESULT (vkCreateImage (this->get_device (), &create_info, nullptr, &this->depth_buffers [i].image));
        vkGetImageMemoryRequirements (this->get_device (), this->depth_buffers [i].image, &this->depth_buffers [i].memReq);

        VkMemoryAllocateInfo mem_alloc {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.allocationSize = this->depth_buffers [i].memReq.size;
        mem_alloc.memoryTypeIndex = vk_utils::findMemoryType (this->depth_buffers [i].memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, this->get_physical_device ());
        VK_CHECK_RESULT (vkAllocateMemory (this->get_device (), &mem_alloc, nullptr, &this->depth_buffers [i].mem));
        VK_CHECK_RESULT (vkBindImageMemory (this->get_device (), this->depth_buffers [i].image, this->depth_buffers [i].mem, 0));

        VkImageViewCreateInfo depth_attachment = vk_utils::defaultImageViewCreateInfo (this->depth_buffers [i].image, this->depth_format, 1, VK_IMAGE_ASPECT_DEPTH_BIT);
        VK_CHECK_RESULT (vkCreateImageView (this->get_device (), &depth_attachment, nullptr, &this->depth_buffers [i].view));
    }

    LOG_INFO ("[VulkanContext] {} {} depth buffers with size ({}, {}).", (recreated) ? "Recreated" : "Created", this->frames_in_swapchain, width, height);
}

void VulkanContext::destroy_swapchain () {
    this->swapchain.Cleanup ();

    for (auto render_finished_semaphore : this->gpu_ready_to_present) {
        if (render_finished_semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore (this->device, render_finished_semaphore, nullptr);
        }
    }

    this->gpu_ready_to_present.clear ();
}

void VulkanContext::destroy_depth_buffers () {
    for (size_t i = 0; i < this->depth_buffers.size (); ++i) {
        vk_utils::deleteImg (this->device, &this->depth_buffers [i]);
    }
    this->depth_buffers.clear ();
}

void VulkanContext::destroy_framebuffers () {
    std::vector <VkFramebuffer> framebuffer;

    for (auto framebuffer : this->main.framebuffer) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->device, framebuffer, nullptr);
    }
    for (auto framebuffer : this->after.framebuffer) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer (this->device, framebuffer, nullptr);
    }

    this->main.framebuffer.clear ();
    this->after.framebuffer.clear ();
}

void VulkanContext::destroy_frame_resources () {
    for (size_t i = 0; i < this->max_frames_in_flight; i++) {
        vkDestroySemaphore (this->device, this->frame_resources [i].wait_before_color_attachment_output, nullptr);
        vkDestroySemaphore (this->device, this->frame_resources [i].wait_before_depth_copy, nullptr);
        vkDestroyFence (this->device, this->frame_resources [i].cpu_wait_next_frame, nullptr);
    }
    this->frame_resources.clear ();
}

}

