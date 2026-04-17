#include <cstdint>

#include "vk_buffers.h"

#include "active_leafs.hpp"

namespace sdf_raster {

ActiveLeafsDescriptorSetInfo create_active_leafs_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , VkDeviceSize active_leafs_size
        , size_t max_frames_in_flight) {
    ActiveLeafsDescriptorSetInfo info = {};

    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    const VkDeviceSize active_leaf_counter_size = sizeof (uint32_t);

    std::vector <VkBuffer> buffers (max_frames_in_flight * 2);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight * 2);

    info.active_leaf_counter_buffers.clear ();
    info.active_leafs_buffers.clear ();

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i * 2 + 0] = vk_utils::createBuffer (device, active_leaf_counter_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 2 + 1]);
        buffers [i * 2 + 1] = vk_utils::createBuffer (device, active_leafs_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 2 + 0]);

        info.active_leaf_counter_buffers.push_back (buffers [i * 2 + 0]);
        info.active_leafs_buffers.push_back (buffers [i * 2 + 1]);
    }

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.active_leaf_counter_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (1, info.active_leafs_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_active_leafs_descriptor_set (VkDevice device, ActiveLeafsDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.active_leafs_buffers.size (); ++i) {
        if (info.active_leaf_counter_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leaf_counter_buffers [i], nullptr);
            info.active_leaf_counter_buffers [i] = VK_NULL_HANDLE;
        }
        if (info.active_leafs_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leafs_buffers [i], nullptr);
            info.active_leafs_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

uint32_t fetch_active_leaf_counter (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t frame) {
    uint32_t active_leafs_count = 0;
    copy_helper->ReadBuffer (info.active_leaf_counter_buffers [frame], 0, &active_leafs_count, sizeof (uint32_t));
    return active_leafs_count;
}

}

