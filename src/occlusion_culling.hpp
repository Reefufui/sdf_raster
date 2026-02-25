#pragma once

#include <memory>
#include <vector>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "vk_images.h"

namespace sdf_raster {

struct HZBufferDescriptorSetInfo {
    struct FrameResources {
        VkImage prev_depth_image = VK_NULL_HANDLE;
        LiteMath::float4x4 prev_view_proj;

        std::vector <VkDescriptorSet> gen_descriptor_sets;
        std::vector <VkImageView> gen_image_views;

        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
        vk_utils::VulkanImageMem hz_buffer;

        VkBuffer transition_buffer = VK_NULL_HANDLE;
    };
    std::vector <FrameResources> frame_resources;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout gen_descriptor_set_layout = VK_NULL_HANDLE;
    VkDeviceMemory transition_memory = VK_NULL_HANDLE;
    VkExtent2D extent;
    VkSampler sampler;
};

HZBufferDescriptorSetInfo create_hz_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent
        , size_t max_frames_in_flight);

void change_hz_buffer_layout_to_shader_read_only_optimal (VkDevice device, VkCommandPool pool, VkQueue queue, HZBufferDescriptorSetInfo& info);

void prepare_next_frame_data (HZBufferDescriptorSetInfo& info, uint32_t frame_idx, VkImage frame_depth_image, LiteMath::float4x4 frame_view_proj);

void cleanup_hz_buffer_descriptor_set (VkDevice device, HZBufferDescriptorSetInfo& info);

}

