#pragma once

#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_images.h>

namespace sdf_raster {

class DummyDescriptorSetInfo {
public:
    DummyDescriptorSetInfo (VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue
        , VkShaderStageFlags shader_stage_flags, VkExtent2D extent, uint32_t num_inflight_frames);
    ~DummyDescriptorSetInfo ();

    VkDescriptorSet get_storage_image_ds () const { return this->storage_image_ds; }
    VkDescriptorSetLayout get_storage_image_ds_layout () const { return this->storage_image_ds_layout; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker;
    VkDescriptorSet storage_image_ds = VK_NULL_HANDLE;
    VkDescriptorSetLayout storage_image_ds_layout = VK_NULL_HANDLE;
    vk_utils::VulkanImageMem storage_image;
};

}
