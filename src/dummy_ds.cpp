#include "dummy_ds.hpp"

#include <vk_utils.h>

namespace sdf_raster {

DummyDescriptorSetInfo::DummyDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , VkCommandPool command_pool
        , VkQueue queue
        , VkShaderStageFlags shader_stage_flags
        , VkExtent2D extent
        , uint32_t num_inflight_frames) : device (device) {
    assert (device != VK_NULL_HANDLE && "VkDevice must not be VK_NULL_HANDLE");
    assert (physical_device != VK_NULL_HANDLE && "VkPhysicalDevice must not be VK_NULL_HANDLE");
    assert (shader_stage_flags != 0 && "shader_stage_flags must specify at least one stage");
    assert (num_inflight_frames > 0 && "num_inflight_frames must be at least 1");

    const uint32_t width = extent.width;
    const uint32_t height = extent.height;
    const uint32_t mip_lvls = 1;
    const VkFormat format = VK_FORMAT_R32_SFLOAT;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT;

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, num_inflight_frames }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, num_inflight_frames);

    this->storage_image.mipLvls = mip_lvls;
    this->storage_image.format = format;
    this->storage_image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vk_utils::createImgAllocAndBind (device, physical_device, width, height, format, usage, &this->storage_image);

    this->desc_maker->BindBegin (shader_stage_flags);
    this->desc_maker->BindImage (0, this->storage_image.view, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    this->desc_maker->BindEnd (&this->storage_image_ds, &this->storage_image_ds_layout);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = 0;
    begin_info.pInheritanceInfo = nullptr;

    VkCommandBuffer cmd_buff = vk_utils::createCommandBuffer (device, command_pool);
    VK_CHECK_RESULT (vkBeginCommandBuffer (cmd_buff, &begin_info));

    VkImageSubresourceRange whole_image {};
    whole_image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    whole_image.baseMipLevel = 0;
    whole_image.levelCount = 1;
    whole_image.baseArrayLayer = 0;
    whole_image.layerCount = 1;

    VkImageMemoryBarrier barr = {};
    barr.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barr.pNext = nullptr;
    barr.srcAccessMask = 0;
    barr.dstAccessMask = 0;
    barr.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barr.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.image = this->storage_image.image;
    barr.subresourceRange = whole_image;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        , 0
        , 0, nullptr
        , 0, nullptr
        , 1, &barr);

    VK_CHECK_RESULT (vkEndCommandBuffer (cmd_buff));
    vk_utils::executeCommandBufferNow (cmd_buff, queue, device);
}

DummyDescriptorSetInfo::~DummyDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();
    vk_utils::deleteImg (device, &this->storage_image);
}

}
