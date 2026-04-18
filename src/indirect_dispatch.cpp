#include "vk_buffers.h"

#include "indirect_dispatch.hpp"
#include "shader_common.hpp"

namespace sdf_raster {

IndirectDescriptorSetInfo::IndirectDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , VkShaderStageFlags shader_stage_flags
    , VkDeviceSize indirect_dispatch_size
    , size_t max_frames_in_flight) : device (device) {

    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    this->indirect_dispatch_buffers.clear ();
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, indirect_dispatch_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i]);
        this->indirect_dispatch_buffers.push_back (buffers [i]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->indirect_dispatch_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

IndirectDescriptorSetInfo::~IndirectDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (size_t i = 0; i < this->indirect_dispatch_buffers.size (); ++i) {
        if (this->indirect_dispatch_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, this->indirect_dispatch_buffers [i], nullptr);
            this->indirect_dispatch_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (this->device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

} // sdf_raster

