// vulkan/context/vulkan_context.cpp
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

#ifdef __APPLE__
#define VK_EXT_METAL_SURFACE_EXTENSION_NAME "VK_EXT_metal_surface"
#endif

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

void VulkanContext::init () {
    VK_CHECK_RESULT (volkInitialize ());
    this->create_instance ();
    this->physical_device = vk_utils::findPhysicalDevice (this->get_instance (), true, 0, {});
    this->create_device ();
    this->create_command_pools ();
    this->get_device_queues ();
    this->copy_helper = std::make_shared <vk_utils::PingPongCopyHelper> (this->get_physical_device ()
            , this->get_device ()
            , this->get_transfer_queue ()
            , this->device_queue_ids.transfer
            , 64 * 1024 * 1024);

    this->initialized = true;
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

#ifdef VULKAN_VALIDATION_LAYERS
    instance_extensions.push_back (VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    instance_extensions.push_back (VK_KHR_SURFACE_EXTENSION_NAME);
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
    LOG_INFO ("--- VkPhysicalDeviceMeshShaderPropertiesEXT ---");
    LOG_INFO ("  maxTaskWorkGroupTotalCount: {}", mesh_shader_properties.maxTaskWorkGroupTotalCount);
    LOG_INFO ("  maxTaskWorkGroupCount: [{}, {}, {}]",
              mesh_shader_properties.maxTaskWorkGroupCount[0],
              mesh_shader_properties.maxTaskWorkGroupCount[1],
              mesh_shader_properties.maxTaskWorkGroupCount[2]);
    LOG_INFO ("  maxTaskWorkGroupInvocations: {}", mesh_shader_properties.maxTaskWorkGroupInvocations);
    LOG_INFO ("  maxTaskWorkGroupSize: [{}, {}, {}]",
              mesh_shader_properties.maxTaskWorkGroupSize[0],
              mesh_shader_properties.maxTaskWorkGroupSize[1],
              mesh_shader_properties.maxTaskWorkGroupSize[2]);
    LOG_INFO ("  maxTaskPayloadSize: {}", mesh_shader_properties.maxTaskPayloadSize);
    LOG_INFO ("  maxTaskSharedMemorySize: {}", mesh_shader_properties.maxTaskSharedMemorySize);
    LOG_INFO ("  maxTaskPayloadAndSharedMemorySize: {}", mesh_shader_properties.maxTaskPayloadAndSharedMemorySize);
    LOG_INFO ("  maxMeshWorkGroupTotalCount: {}", mesh_shader_properties.maxMeshWorkGroupTotalCount);
    LOG_INFO ("  maxMeshWorkGroupCount: [{}, {}, {}]",
              mesh_shader_properties.maxMeshWorkGroupCount[0],
              mesh_shader_properties.maxMeshWorkGroupCount[1],
              mesh_shader_properties.maxMeshWorkGroupCount[2]);
    LOG_INFO ("  maxMeshWorkGroupInvocations: {}", mesh_shader_properties.maxMeshWorkGroupInvocations);
    LOG_INFO ("  maxMeshWorkGroupSize: [{}, {}, {}]",
              mesh_shader_properties.maxMeshWorkGroupSize[0],
              mesh_shader_properties.maxMeshWorkGroupSize[1],
              mesh_shader_properties.maxMeshWorkGroupSize[2]);
    LOG_INFO ("  maxMeshSharedMemorySize: {}", mesh_shader_properties.maxMeshSharedMemorySize);
    LOG_INFO ("  maxMeshPayloadAndSharedMemorySize: {}", mesh_shader_properties.maxMeshPayloadAndSharedMemorySize);
    LOG_INFO ("  maxMeshOutputMemorySize: {}", mesh_shader_properties.maxMeshOutputMemorySize);
    LOG_INFO ("  maxMeshPayloadAndOutputMemorySize: {}", mesh_shader_properties.maxMeshPayloadAndOutputMemorySize);
    LOG_INFO ("  maxMeshOutputComponents: {}", mesh_shader_properties.maxMeshOutputComponents);
    LOG_INFO ("  maxMeshOutputVertices: {}", mesh_shader_properties.maxMeshOutputVertices);
    LOG_INFO ("  maxMeshOutputPrimitives: {}", mesh_shader_properties.maxMeshOutputPrimitives);
    LOG_INFO ("  maxMeshOutputLayers: {}", mesh_shader_properties.maxMeshOutputLayers);
    LOG_INFO ("  maxMeshMultiviewViewCount: {}", mesh_shader_properties.maxMeshMultiviewViewCount);
    LOG_INFO ("  meshOutputPerVertexGranularity: {}", mesh_shader_properties.meshOutputPerVertexGranularity);
    LOG_INFO ("  meshOutputPerPrimitiveGranularity: {}", mesh_shader_properties.meshOutputPerPrimitiveGranularity);
    LOG_INFO ("  maxPreferredTaskWorkGroupInvocations: {}", mesh_shader_properties.maxPreferredTaskWorkGroupInvocations);
    LOG_INFO ("  maxPreferredMeshWorkGroupInvocations: {}", mesh_shader_properties.maxPreferredMeshWorkGroupInvocations);
    LOG_INFO ("  prefersLocalInvocationVertexOutput: {}", mesh_shader_properties.prefersLocalInvocationVertexOutput ? "true" : "false");
    LOG_INFO ("  prefersLocalInvocationPrimitiveOutput: {}", mesh_shader_properties.prefersLocalInvocationPrimitiveOutput ? "true" : "false");
    LOG_INFO ("  prefersCompactVertexOutput: {}", mesh_shader_properties.prefersCompactVertexOutput ? "true" : "false");
    LOG_INFO ("  prefersCompactPrimitiveOutput: {}", mesh_shader_properties.prefersCompactPrimitiveOutput ? "true" : "false");
    LOG_INFO ("-------------------------------------------");
}

void VulkanContext::create_device () {
    std::vector <const char*> validation_layers_to_enable {};
    std::vector <const char*> device_extensions_to_enable {};

    device_extensions_to_enable.push_back (VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef __APPLE__
    device_extensions_to_enable.push_back (VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

    VkPhysicalDeviceFeatures2 device_features_2 {};
    device_features_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    void* pNext_query_chain = nullptr;
    void* pNext_create_chain = nullptr;

    VkPhysicalDeviceVulkan11Features vulkan11_features_query {};
    vulkan11_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vulkan11_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &vulkan11_features_query;

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

    VkPhysicalDeviceVulkan13Features vulkan13_features_query {};
    vulkan13_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &vulkan13_features_query;

    VkPhysicalDeviceMeshShaderFeaturesEXT mesh_shader_features_query {};
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties_query {};

    mesh_shader_features_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    mesh_shader_features_query.pNext = pNext_query_chain;
    pNext_query_chain = &mesh_shader_features_query;

    mesh_shader_properties_query.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
    mesh_shader_properties_query.pNext = nullptr;

    device_features_2.pNext = pNext_query_chain;

    vkGetPhysicalDeviceFeatures2 (this->get_physical_device (), &device_features_2);

    VkPhysicalDeviceProperties2 properties_2 {};
    properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties_2.pNext = &mesh_shader_properties_query;
    vkGetPhysicalDeviceProperties2 (this->get_physical_device (), &properties_2);

    if (!device_features_2.features.wideLines) {
        LOG_WARN ("[VulkanContext] Physical device does NOT support wideLines. Defaulting to lineWidth = 1.0.");
    }

    VkPhysicalDeviceVulkan11Features vulkan11_features_enable {};
    if (vulkan11_features_query.shaderDrawParameters) {
        vulkan11_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        vulkan11_features_enable.shaderDrawParameters = VK_TRUE; 
        vulkan11_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &vulkan11_features_enable;
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
        vulkan_memory_model_features_enable.vulkanMemoryModelDeviceScope = vulkan_memory_model_features_query.vulkanMemoryModelDeviceScope;
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
    if (mesh_shader_features_query.taskShader && mesh_shader_features_query.meshShader) {
        requested_mesh_shader_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        requested_mesh_shader_features_enable.taskShader = VK_TRUE;
        requested_mesh_shader_features_enable.meshShader = VK_TRUE;
        requested_mesh_shader_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &requested_mesh_shader_features_enable;

        this->use_mesh_shading = true;
        this->mesh_shader_properties = mesh_shader_properties_query;
        this->dump_mesh_shader_properties ();
        device_extensions_to_enable.push_back (VK_EXT_MESH_SHADER_EXTENSION_NAME);
    } else {
        this->use_mesh_shading = false;
        LOG_WARN ("[VulkanContext] Mesh Shaders are NOT supported on this physical device.");
    }

    VkPhysicalDeviceVulkan13Features vulkan13_features_enable {};
    if (vulkan13_features_query.shaderIntegerDotProduct) {
        vulkan13_features_enable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13_features_enable.shaderIntegerDotProduct = VK_TRUE;
        vulkan13_features_enable.pNext = pNext_create_chain;
        pNext_create_chain = &vulkan13_features_enable;
    }

    VkPhysicalDeviceFeatures features_to_enable_in_base_struct = device_features_2.features;
#ifdef __APPLE__
    features_to_enable_in_base_struct.robustBufferAccess = VK_FALSE;
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

        auto destroy_command_pool = [&](VkCommandPool& command_pool) {
            if (command_pool != VK_NULL_HANDLE) {
                vkDestroyCommandPool (this->device, command_pool, nullptr);
                command_pool = VK_NULL_HANDLE;
            }
        };
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

}
