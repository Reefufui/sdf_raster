// resources/active_leafs.cpp
#include "vk_buffers.h"
#include "resources/active_leafs.hpp"

namespace sdf_raster {

ActiveLeafsDescriptorSetInfo::ActiveLeafsDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , VkDeviceSize active_leafs_size
        , size_t max_frames_in_flight) : device (device), copy_helper (copy_helper) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    const VkDeviceSize active_leaf_counter_size = sizeof (uint32_t);

    std::vector <VkBuffer> buffers (max_frames_in_flight * 2);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight * 2);

    this->active_leaf_counter_buffers.clear ();
    this->active_leafs_buffers.clear ();

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i * 2 + 0] = vk_utils::createBuffer (device, active_leaf_counter_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 2 + 1]);
        buffers [i * 2 + 1] = vk_utils::createBuffer (device, active_leafs_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 2 + 0]);

        this->active_leaf_counter_buffers.push_back (buffers [i * 2 + 0]);
        this->active_leafs_buffers.push_back (buffers [i * 2 + 1]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->active_leaf_counter_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (1, this->active_leafs_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

ActiveLeafsDescriptorSetInfo::~ActiveLeafsDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (size_t i = 0; i < this->active_leafs_buffers.size (); ++i) {
        if (this->active_leaf_counter_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, this->active_leaf_counter_buffers [i], nullptr);
            this->active_leaf_counter_buffers [i] = VK_NULL_HANDLE;
        }
        if (this->active_leafs_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, this->active_leafs_buffers [i], nullptr);
            this->active_leafs_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (this->device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

uint32_t ActiveLeafsDescriptorSetInfo::fetch_active_leaf_counter (uint32_t fif_index) {
    if (!this->copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    uint32_t active_leafs_count = 0;
    this->copy_helper->ReadBuffer (this->active_leaf_counter_buffers [fif_index], 0, &active_leafs_count, sizeof (uint32_t));
    return active_leafs_count;
}

} // sdf_raster