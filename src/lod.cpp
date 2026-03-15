#include <fstream>

#include "vk_buffers.h"
#include "lod.hpp"

namespace sdf_raster {

LODDescriptorSetInfo create_lod_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags
    , size_t count
    , size_t max_frames_in_flight) {
    LODDescriptorSetInfo info = {};

    VkDeviceSize lods_size = count * sizeof (LevelOfDetail);

    if (count == 0) {
        throw std::runtime_error ("create_lod_descriptor_set: lod count is 0, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    info.lod_buffers.clear ();
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, lods_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i]);
        info.lod_buffers.push_back (buffers [i]);
    }

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.lod_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_lod_descriptor_set (VkDevice device, LODDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.lod_buffers.size (); ++i) {
        if (info.lod_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.lod_buffers [i], nullptr);
            info.lod_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

LevelOfDetail fetch_lod (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, LODDescriptorSetInfo info, size_t frame, size_t index) {
    LevelOfDetail lod;
    copy_helper->ReadBuffer (info.lod_buffers [frame], index * sizeof (LevelOfDetail), &lod, sizeof (LevelOfDetail));
    return lod;
}

}

