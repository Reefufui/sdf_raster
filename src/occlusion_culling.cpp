#include "occlusion_culling.hpp"

#include "vk_utils.h"

#include "logger.hpp"

namespace sdf_raster {

HZBufferDescriptorSetInfo create_hz_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent) {
    assert (device != VK_NULL_HANDLE && "VkDevice must not be VK_NULL_HANDLE");
    assert (physical_device != VK_NULL_HANDLE && "VkPhysicalDevice must not be VK_NULL_HANDLE");
    assert (shader_stage_flags != 0 && "shader_stage_flags should specify at least one stage");

    HZBufferDescriptorSetInfo info = {};

    const uint32_t width = swapchain_extent.width;
    const uint32_t height = swapchain_extent.height;
    assert (width > 0 && "Swapchain width must be positive");
    assert (height > 0 && "Swapchain height must be positive");

    info.hz_buffer.mipLvls = std::log2 (std::max (width, height)) + 1;
    info.hz_buffer.format = VK_FORMAT_R32_SFLOAT;

    assert (info.hz_buffer.mipLvls > 0 && "Calculated mipLvls must be positive");

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo image_info = vk_utils::defaultImageCreateInfo (width, height, info.hz_buffer.format, usage, info.hz_buffer.mipLvls);
    VK_CHECK_RESULT (vkCreateImage (device, &image_info, nullptr, &info.hz_buffer.image));
    vkGetImageMemoryRequirements (device, info.hz_buffer.image, &info.hz_buffer.memReq);

    VkMemoryAllocateInfo mem_alloc {};
    mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mem_alloc.allocationSize = info.hz_buffer.memReq.size;
    mem_alloc.memoryTypeIndex = vk_utils::findMemoryType (info.hz_buffer.memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, physical_device);
    VK_CHECK_RESULT (vkAllocateMemory (device, &mem_alloc, nullptr, &info.hz_buffer.mem));
    VK_CHECK_RESULT (vkBindImageMemory (device, info.hz_buffer.image, info.hz_buffer.mem, 0));

    VkImageViewCreateInfo view_info = vk_utils::defaultImageViewCreateInfo (info.hz_buffer.image, info.hz_buffer.format, info.hz_buffer.mipLvls, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK_RESULT (vkCreateImageView (device, &view_info, nullptr, &info.hz_buffer.view));

    info.gen_image_views.resize (info.hz_buffer.mipLvls);
    for (size_t i = 0; i < info.hz_buffer.mipLvls; ++i) {
        view_info.subresourceRange.baseMipLevel = i;
        view_info.subresourceRange.levelCount = 1;
        VK_CHECK_RESULT (vkCreateImageView (device, &view_info, nullptr, &info.gen_image_views [i]));
    }

    info.gen_descriptor_sets.resize (info.hz_buffer.mipLvls - 1);
    for (size_t i = 0; i < info.hz_buffer.mipLvls - 1; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindImage (0, info.gen_image_views [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
        ds_maker.BindImage (1, info.gen_image_views [i + 1], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
        ds_maker.BindEnd (&info.gen_descriptor_sets [i], &info.gen_descriptor_set_layout);
    }

    VkSamplerCreateInfo sampler_info = {};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.maxAnisotropy = 1.0f;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.compareOp = VK_COMPARE_OP_ALWAYS;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = static_cast <float> (info.hz_buffer.mipLvls);
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    VK_CHECK_RESULT (vkCreateSampler (device, &sampler_info, nullptr, &info.sampler));

    ds_maker.BindBegin (shader_stage_flags);
    ds_maker.BindImage (0, info.hz_buffer.view, info.sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    ds_maker.BindEnd (&info.descriptor_set, &info.descriptor_set_layout);

    LOG_INFO ("Created HZ-buffer for occlusion culling ({}, {}) with {} mip levels.", width, height, info.hz_buffer.mipLvls);

    return info;
}

void cleanup_hz_buffer_descriptor_set (VkDevice device, HZBufferDescriptorSetInfo& info) {
    vk_utils::deleteImg (device, &info.hz_buffer);

    for (auto& view : info.gen_image_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView (device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }

    if (info.sampler != VK_NULL_HANDLE) {
        vkDestroySampler (device, info.sampler, nullptr);
        info.sampler = VK_NULL_HANDLE;
    }

    info.gen_descriptor_sets.clear ();
    info.gen_image_views.clear ();

    info = {};
}

}

