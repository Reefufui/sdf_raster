#include "occlusion_culling.hpp"

#include "vk_utils.h"

namespace sdf_raster {

HZBufferDescriptorSetInfo create_hz_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent
        , size_t max_frames_in_flight) {
    const uint32_t width = swapchain_extent.width;
    const uint32_t height = swapchain_extent.height;

    assert (device != VK_NULL_HANDLE && "VkDevice must not be VK_NULL_HANDLE");
    assert (physical_device != VK_NULL_HANDLE && "VkPhysicalDevice must not be VK_NULL_HANDLE");
    assert (ds_maker.GetPool () != VK_NULL_HANDLE && "VkDescriptorPool of vk_utils::DescriptorMaker must not be VK_NULL_HANDLE");
    assert (shader_stage_flags != 0 && "shader_stage_flags must specify at least one stage");
    assert (width > 0 && "Swapchain width must be positive");
    assert (height > 0 && "Swapchain height must be positive");
    assert (max_frames_in_flight > 0 && "max_frames_in_flight must be at least 1.");

    const uint32_t mip_lvls = static_cast <uint32_t> (std::floor (std::log2 (std::max (width, height)) + 1));

    HZBufferDescriptorSetInfo info = {};
    info.extent = swapchain_extent;
    info.frame_resources.resize (max_frames_in_flight);

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
    sampler_info.maxLod = static_cast <float> (mip_lvls);
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;
    VK_CHECK_RESULT (vkCreateSampler (device, &sampler_info, nullptr, &info.sampler));

    const VkFormat format = VK_FORMAT_R32_SFLOAT;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo image_info = vk_utils::defaultImageCreateInfo (width, height, format, usage, mip_lvls);

    for (size_t frame_idx = 0; frame_idx < max_frames_in_flight; ++frame_idx) {
        HZBufferDescriptorSetInfo::FrameResources& f = info.frame_resources [frame_idx];
        f.hz_buffer.mipLvls = mip_lvls;
        f.hz_buffer.format = format;
        f.prev_depth_image = VK_NULL_HANDLE;
        f.prev_view_proj = LiteMath::float4x4 {};

        VK_CHECK_RESULT (vkCreateImage (device, &image_info, nullptr, &f.hz_buffer.image));
        vkGetImageMemoryRequirements (device, f.hz_buffer.image, &f.hz_buffer.memReq);
        VkMemoryAllocateInfo mem_alloc {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.allocationSize = f.hz_buffer.memReq.size;
        mem_alloc.memoryTypeIndex = vk_utils::findMemoryType (f.hz_buffer.memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, physical_device);
        VK_CHECK_RESULT (vkAllocateMemory (device, &mem_alloc, nullptr, &f.hz_buffer.mem));
        VK_CHECK_RESULT (vkBindImageMemory (device, f.hz_buffer.image, f.hz_buffer.mem, 0));

        VkImageViewCreateInfo view_info = vk_utils::defaultImageViewCreateInfo (f.hz_buffer.image, format, mip_lvls, VK_IMAGE_ASPECT_COLOR_BIT);
        VK_CHECK_RESULT (vkCreateImageView (device, &view_info, nullptr, &f.hz_buffer.view));

        f.gen_image_views.resize (mip_lvls);
        for (size_t i = 0; i < mip_lvls; ++i) {
            view_info.subresourceRange.baseMipLevel = i;
            view_info.subresourceRange.levelCount = 1;
            VK_CHECK_RESULT (vkCreateImageView (device, &view_info, nullptr, &f.gen_image_views [i]));
        }

        f.gen_descriptor_sets.resize (f.hz_buffer.mipLvls - 1);
        for (size_t i = 0; i < mip_lvls - 1; ++i) {
            ds_maker.BindBegin (shader_stage_flags);
            ds_maker.BindImage (0, f.gen_image_views [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            ds_maker.BindImage (1, f.gen_image_views [i + 1], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            ds_maker.BindEnd (&f.gen_descriptor_sets [i], &info.gen_descriptor_set_layout);
        }

        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindImage (0, f.hz_buffer.view, info.sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        ds_maker.BindEnd (&f.descriptor_set, &info.descriptor_set_layout);
    }

    return info;
}

void prepare_next_frame_data (HZBufferDescriptorSetInfo& info, uint32_t frame_idx, VkImage frame_depth_image, LiteMath::float4x4 frame_view_proj) {
    info.frame_resources [frame_idx].prev_depth_image = frame_depth_image;
    info.frame_resources [frame_idx].prev_view_proj = frame_view_proj;
}

void cleanup_hz_buffer_descriptor_set (VkDevice device, HZBufferDescriptorSetInfo& info) {
    for (auto& f : info.frame_resources) {
        vk_utils::deleteImg (device, &f.hz_buffer);

        for (auto& view : f.gen_image_views) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView (device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }

        f.gen_descriptor_sets.clear ();
        f.gen_image_views.clear ();
    }

    info.frame_resources.clear ();

    if (info.sampler != VK_NULL_HANDLE) {
        vkDestroySampler (device, info.sampler, nullptr);
        info.sampler = VK_NULL_HANDLE;
    }

    info = {};
}

}

