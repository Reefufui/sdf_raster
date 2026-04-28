// resources/frustum.cpp
#include "shader_common.hpp"
#include "resources/frustum.hpp"

#include <vk_buffers.h>
#include <vk_utils.h>

namespace sdf_raster {

FrustumDescriptorSetInfo::FrustumDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , VkShaderStageFlags shader_stage_flags
    , size_t max_frames_in_flight) : device (device) {
    VkDeviceSize frustum_geometry_size = sizeof (FrustumGeometry);

    this->frustum_geometry_buffers.resize (max_frames_in_flight);
    this->frustum_geometry_memories.resize (max_frames_in_flight);
    this->frustum_geometry_memories_mapped.resize (max_frames_in_flight);

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        VkMemoryRequirements mem_req;
        this->frustum_geometry_buffers [i] = vk_utils::createBuffer (device, frustum_geometry_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &mem_req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mem_req.size;
        allocInfo.memoryTypeIndex = vk_utils::findMemoryType (mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, physical_device);

        VK_CHECK_RESULT (vkAllocateMemory (device, &allocInfo, nullptr, &(this->frustum_geometry_memories [i])));

        vkBindBufferMemory (device, this->frustum_geometry_buffers [i], this->frustum_geometry_memories [i], 0);

        VK_CHECK_RESULT (vkMapMemory (device, this->frustum_geometry_memories [i], 0, frustum_geometry_size, 0, &(this->frustum_geometry_memories_mapped [i])));
    }

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->frustum_geometry_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

FrustumDescriptorSetInfo::~FrustumDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (size_t i = 0; i < this->frustum_geometry_buffers.size (); ++i) {
        if (this->frustum_geometry_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, this->frustum_geometry_buffers [i], nullptr);
            this->frustum_geometry_buffers [i] = VK_NULL_HANDLE;
        }
        if (this->frustum_geometry_memories [i] != VK_NULL_HANDLE) {
            vkFreeMemory (this->device, this->frustum_geometry_memories [i], nullptr);
            this->frustum_geometry_memories [i] = VK_NULL_HANDLE;
        }
    }
}

}

