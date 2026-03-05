#include "vk_buffers.h"

#include "indirect_dispatch.hpp"
#include "shader_common.hpp"

namespace sdf_raster {

IndirectDispatchDescriptorSetInfo create_indirect_dispatch_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags
    , size_t max_frames_in_flight) {
    IndirectDispatchDescriptorSetInfo info = {};

    VkDeviceSize indirect_dispatch_size = sizeof (IndirectDispatch);

    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    info.indirect_dispatch_buffers.clear ();
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, indirect_dispatch_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i]);
        info.indirect_dispatch_buffers.push_back (buffers [i]);
    }

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.indirect_dispatch_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_indirect_dispatch_descriptor_set (VkDevice device, IndirectDispatchDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.indirect_dispatch_buffers.size (); ++i) {
        if (info.indirect_dispatch_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.indirect_dispatch_buffers [i], nullptr);
            info.indirect_dispatch_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

}

