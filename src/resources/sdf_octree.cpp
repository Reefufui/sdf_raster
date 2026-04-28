// resources/sdf_octree.cpp
#include "resources/sdf_octree.hpp"

#include <vk_buffers.h>

#include <cstdint>
#include <fstream>
#include <stack>
#include <future>


namespace sdf_raster {

SdfOctreeDescriptorSetInfo::SdfOctreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , std::shared_ptr <SdfOctreeScene> scene
        , size_t max_frames_in_flight) : device (device) , scene (scene) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    const SdfOctree& scene_data = scene->get_octree_data ();
    const SceneState& scene_state = scene->get_state ();

    VkDeviceSize octree_nodes_size = scene_data.nodes.size () * sizeof (SdfOctreeNode);
    VkDeviceSize subtree_size = (1LL << (3 * scene_state.cpu_traversed)) * sizeof (NodeContext);

    if (octree_nodes_size == 0) {
        throw std::runtime_error ("SdfOctree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (1 + max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (1 + max_frames_in_flight);

    this->subtree_root_buffers.clear ();

    buffers [0] = vk_utils::createBuffer (device, octree_nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    this->nodes_buffer = buffers [0];

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i + 1] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i + 1]);
        this->subtree_root_buffers.push_back (buffers [i + 1]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (this->nodes_buffer, 0, scene_data.nodes.data (), octree_nodes_size);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->nodes_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (1, this->subtree_root_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }

    this->subtree_roots_staging_buffers.resize (max_frames_in_flight);
    this->staging_buffer_memories.resize (max_frames_in_flight);
    this->subtrees_memory_mapped.resize (max_frames_in_flight);

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        VkMemoryRequirements mem_req;
        this->subtree_roots_staging_buffers [i] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &mem_req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mem_req.size;
        allocInfo.memoryTypeIndex = vk_utils::findMemoryType (mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, physical_device);

        VK_CHECK_RESULT (vkAllocateMemory (this->device, &allocInfo, nullptr, &this->staging_buffer_memories [i]));
        vkBindBufferMemory (this->device, this->subtree_roots_staging_buffers [i], this->staging_buffer_memories [i], 0);
        VK_CHECK_RESULT (vkMapMemory (this->device, this->staging_buffer_memories [i], 0, subtree_size, 0, &this->subtrees_memory_mapped [i]));
    }

    this->mapped_memory_size = subtree_size;
}

SdfOctreeDescriptorSetInfo::~SdfOctreeDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    for (size_t i = 0; i < this->subtree_roots_staging_buffers.size (); ++i) {
        if (this->subtree_roots_staging_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->subtree_roots_staging_buffers [i], nullptr);
            this->subtree_roots_staging_buffers [i] = VK_NULL_HANDLE;
        }

        vkUnmapMemory (this->device, this->staging_buffer_memories [i]);
        vkFreeMemory (this->device, this->staging_buffer_memories [i], nullptr);
        this->staging_buffer_memories [i] = VK_NULL_HANDLE;
    }

    this->desc_maker.reset ();

    if (this->nodes_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, this->nodes_buffer, nullptr);
        this->nodes_buffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < this->subtree_root_buffers.size (); ++i) {
        if (this->subtree_root_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->subtree_root_buffers [i], nullptr);
            this->subtree_root_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

void SdfOctreeDescriptorSetInfo::update_subtree_root_buffer (const FrustumGeometry& frustum, uint32_t fif_index) {
    assert (this->scene);
    auto visible_subtrees = this->scene->collect_visible_subtrees (frustum);
    this->subtree_count = visible_subtrees.size ();
    if (this->subtree_count) {
        memcpy (this->subtrees_memory_mapped [fif_index], visible_subtrees.data (), this->mapped_memory_size);
    }
}

} // sdf_raster