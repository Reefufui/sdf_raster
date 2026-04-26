#include "occlusion_culling.hpp"

#include <vk_buffers.h>
#include <vk_utils.h>

namespace sdf_raster {

HZBufferDescriptorSetInfo::HZBufferDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , VkCommandPool command_pool
        , VkQueue queue
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D swapchain_extent
        , size_t max_frames_in_flight) : device (device) {
    const uint32_t width = swapchain_extent.width;
    const uint32_t height = swapchain_extent.height;

    assert (device != VK_NULL_HANDLE && "VkDevice must not be VK_NULL_HANDLE");
    assert (physical_device != VK_NULL_HANDLE && "VkPhysicalDevice must not be VK_NULL_HANDLE");
    assert (shader_stage_flags != 0 && "shader_stage_flags must specify at least one stage");
    assert (width > 0 && "Swapchain width must be positive");
    assert (height > 0 && "Swapchain height must be positive");
    assert (max_frames_in_flight > 0 && "max_frames_in_flight must be at least 1.");

    const uint32_t mip_lvls = static_cast <uint32_t> (std::floor (std::log2 (std::max (width, height)) + 1));

    this->extent = swapchain_extent;
    this->frame_resources.resize (max_frames_in_flight);

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
    VK_CHECK_RESULT (vkCreateSampler (device, &sampler_info, nullptr, &this->sampler));

    const VkFormat format = VK_FORMAT_R32_SFLOAT;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateInfo image_info = vk_utils::defaultImageCreateInfo (width, height, format, usage, mip_lvls);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, max_frames_in_flight }
        , { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, (2 * mip_lvls + 1) * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight + mip_lvls * max_frames_in_flight);

    for (size_t frame_idx = 0; frame_idx < max_frames_in_flight; ++frame_idx) {
        HZBufferDescriptorSetInfo::FrameResources& f = this->frame_resources [frame_idx];
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
            this->desc_maker->BindBegin (shader_stage_flags);
            this->desc_maker->BindImage (0, f.gen_image_views [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            this->desc_maker->BindImage (1, f.gen_image_views [i + 1], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
            this->desc_maker->BindEnd (&f.gen_descriptor_sets [i], &this->gen_descriptor_set_layout);
        }

        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindImage (0, f.gen_image_views [0], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
        this->desc_maker->BindEnd (&f.base_level_descriptor_set, &this->base_level_descriptor_set_layout);

        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindImage (0, f.hz_buffer.view, this->sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        this->desc_maker->BindEnd (&f.descriptor_set, &this->descriptor_set_layout);
    }

    const VkDeviceSize transition_size = width * height * sizeof (float);
    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, transition_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &mem_reqs [i]);
        this->frame_resources [i].transition_buffer = buffers [i];
    }

    this->transition_memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VkCommandBuffer cmd_buff = vk_utils::createCommandBuffer (device, command_pool);
    VK_CHECK_RESULT (vkBeginCommandBuffer (cmd_buff, &begin_info));

    VkImageSubresourceRange whole_image {};
    whole_image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    whole_image.baseMipLevel = 0;
    whole_image.levelCount = this->frame_resources [0].hz_buffer.mipLvls;
    whole_image.baseArrayLayer = 0;
    whole_image.layerCount = 1;

    VkImageMemoryBarrier barr = {};
    barr.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barr.pNext = nullptr;
    barr.srcAccessMask = 0;
    barr.dstAccessMask = 0;
    barr.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barr.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.subresourceRange = whole_image;

    for (size_t i = 0; i < this->frame_resources.size (); ++i) {
        barr.image = this->frame_resources [i].hz_buffer.image;

        vkCmdPipelineBarrier (cmd_buff
            , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
            , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
            , 0
            , 0, nullptr
            , 0, nullptr
            , 1, &barr);
    }

    VK_CHECK_RESULT (vkEndCommandBuffer (cmd_buff));
    vk_utils::executeCommandBufferNow (cmd_buff, queue, device);
}

HZBufferDescriptorSetInfo::~HZBufferDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (auto& f : this->frame_resources) {
        vk_utils::deleteImg (device, &f.hz_buffer);

        for (auto& view : f.gen_image_views) {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView (device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
        }

        f.gen_descriptor_sets.clear ();
        f.gen_image_views.clear ();

        if (f.transition_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, f.transition_buffer, nullptr);
            f.transition_buffer = VK_NULL_HANDLE;
        }
    }

    this->frame_resources.clear ();

    if (this->sampler != VK_NULL_HANDLE) {
        vkDestroySampler (device, this->sampler, nullptr);
        this->sampler = VK_NULL_HANDLE;
    }

    if (this->transition_memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, this->transition_memory, nullptr);
        this->transition_memory = VK_NULL_HANDLE;
    }
}

}

