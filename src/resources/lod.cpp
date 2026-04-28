// resources/lod.cpp
#include "vk_buffers.h"
#include "resources/lod.hpp"

namespace sdf_raster {

LODDescriptorSetInfo::LODDescriptorSetInfo (VkDevice device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , VkPhysicalDevice physical_device
    , VkShaderStageFlags shader_stage_flags
    , size_t count
    , size_t max_frames_in_flight) : device (device), copy_helper (copy_helper) {
    VkDeviceSize lods_size = count * sizeof (LevelOfDetail);

    if (count == 0) {
        throw std::runtime_error ("create_lod_descriptor_set: lod count is 0, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    this->lod_buffers.clear ();
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, lods_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i]);
        this->lod_buffers.push_back (buffers [i]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->lod_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

LODDescriptorSetInfo::~LODDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (size_t i = 0; i < this->lod_buffers.size (); ++i) {
        if (this->lod_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->lod_buffers [i], nullptr);
            this->lod_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

LevelOfDetail LODDescriptorSetInfo::fetch_lod (size_t frame, size_t index) {
    LevelOfDetail lod;
    this->copy_helper->ReadBuffer (this->lod_buffers [frame], index * sizeof (LevelOfDetail), &lod, sizeof (LevelOfDetail));
    return lod;
}

} // sdf_raster