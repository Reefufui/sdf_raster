#pragma once

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_images.h>

#include <memory>
#include <vector>

namespace sdf_raster {

class HZBufferDescriptorSetInfo {
public:
    HZBufferDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , VkCommandPool command_pool
        , VkQueue queue
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent
        , size_t max_frames_in_flight);
    ~HZBufferDescriptorSetInfo ();

    VkDescriptorSetLayout get_gen_layout () const { return this->gen_descriptor_set_layout; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    struct FrameResources {
        VkImage prev_depth_image = VK_NULL_HANDLE;
        LiteMath::float4x4 prev_view_proj;

        std::vector <VkDescriptorSet> gen_descriptor_sets;
        std::vector <VkImageView> gen_image_views;

        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        vk_utils::VulkanImageMem hz_buffer;

        VkBuffer transition_buffer = VK_NULL_HANDLE;
    };
    FrameResources& frame_resources_ref (uint32_t fif_index) { return this->frame_resources [fif_index]; }
    const VkExtent2D& get_extent () const { return this->extent; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine> copy_helper;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout gen_descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <FrameResources> frame_resources;
    VkDeviceMemory transition_memory = VK_NULL_HANDLE;
    VkExtent2D extent;
    VkSampler sampler;
};

}

