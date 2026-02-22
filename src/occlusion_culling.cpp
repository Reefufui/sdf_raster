#include "occlusion_culling.hpp"

#include "vk_utils.h"

namespace sdf_raster {

DepthBufferDescriptorSetInfo create_depth_buffer_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent) {
    DepthBufferDescriptorSetInfo info = {};

    const uint32_t width = swapchain_extent.width;
    const uint32_t height = swapchain_extent.height;
    assert (width > 0 && height > 0);

    info.hz_buffer.mipLvls = std::log2 (std::max (width, height)) + 1;
    info.hz_buffer.format = VK_FORMAT_R32_SFLOAT;

    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

    ds_maker.BindBegin (shader_stage_flags);
    ds_maker.BindImage (0, info.hz_buffer.view, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    ds_maker.BindEnd (&info.descriptor_set, &info.descriptor_set_layout);

    // TODO: change layout to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    // VkImageSubresourceRange subresourceRange = {};
    // subresourceRange.baseMipLevel = i;
    // subresourceRange.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
    // subresourceRange.levelCount              = 1;
    // subresourceRange.layerCount              = 1;
    // vk_utils::setImageLayout(
    //     a_cmdBuf,
    //     a_image,
    //     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    //     a_targetLayout,
    //     subresourceRange);

    return info;
}

}

