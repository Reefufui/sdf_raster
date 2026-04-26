#pragma once

#include <memory>
#include <vector>

#include "vk_context.h"
#include "vk_utils.h"

namespace sdf_raster {

class VulkanContext {
public:
    void init ();
    void shutdown ();

    inline bool is_initialized () const { return this->initialized; };
    inline VkInstance get_instance () const { return this->instance; }
    inline VkPhysicalDevice get_physical_device () const { return this->physical_device; }
    inline VkDevice get_device () const { return this->device; }
    inline VkCommandPool get_compute_command_pool_reset () const { return this->compute_command_pool_reset; }
    inline VkCommandPool get_compute_command_pool_transistent () const { return this->compute_command_pool_transistent; }
    inline VkCommandPool get_graphics_command_pool_reset () const { return this->graphics_command_pool_reset; }
    inline VkCommandPool get_graphics_command_pool_transistent () const { return this->graphics_command_pool_transistent; }
    inline VkCommandPool get_transfer_command_pool_reset () const { return this->transfer_command_pool_reset; }
    inline VkCommandPool get_transfer_command_pool_transistent () const { return this->transfer_command_pool_transistent; }
    inline VkQueue get_compute_queue () const { return this->compute_queue; }
    inline VkQueue get_graphics_queue () const { return this->graphics_queue; }
    inline VkQueue get_transfer_queue () const { return this->transfer_queue; }
    inline uint32_t get_graphics_queue_family_index () const { return this->device_queue_ids.graphics; }
    inline std::shared_ptr <vk_utils::ICopyEngine> get_copy_helper () const { return this->copy_helper; }
    inline bool get_use_mesh_shading () const { return this->use_mesh_shading; }

private:
    void create_instance ();
    void dump_mesh_shader_properties () const;
    void create_device ();
    void create_command_pools ();
    void get_device_queues ();

#ifdef VULKAN_VALIDATION_LAYERS
    void setup_debug_utils_messenger ();
    VkDebugUtilsMessengerEXT debug_utils_messenger = VK_NULL_HANDLE;
#endif

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    vk_utils::QueueFID_T device_queue_ids {};
    VkCommandPool compute_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool compute_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool graphics_command_pool_transistent = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_reset = VK_NULL_HANDLE;
    VkCommandPool transfer_command_pool_transistent = VK_NULL_HANDLE;
    VkQueue compute_queue = VK_NULL_HANDLE;
    VkQueue transfer_queue = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    std::shared_ptr <vk_utils::ICopyEngine> copy_helper = nullptr;

    bool use_mesh_shading;
    VkPhysicalDeviceMeshShaderPropertiesEXT mesh_shader_properties;

    bool initialized = false;
};

}
