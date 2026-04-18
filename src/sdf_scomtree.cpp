#include "sdf_scomtree.hpp"

#include "logger.hpp"
#include "scenes/scomtree/defs.hpp"
#include "scenes/scomtree/rotation_lookup_tables.hpp"

#include <vk_buffers.h>

#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

namespace sdf_raster {

float sample_sdf (const SComTree& /*scene*/, const LiteMath::float3& /*p*/) {
    // TODO
    assert (false);
    return 0.f;
}

SComTreeTreeDescriptorSetInfo::SComTreeTreeDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , VkShaderStageFlags shader_stage_flags
    , const SComTree& scomtree
    , const size_t subtree_root_level
    , size_t max_frames_in_flight) : device (device) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;

    VkDeviceSize header_size = sizeof (SComTreeHeader);
    VkDeviceSize nodes_size = scomtree.nodes.size () * sizeof (uint32_t);
    VkDeviceSize bricks_size = scomtree.bricks.size () * sizeof (uint32_t);
    VkDeviceSize rotation_modifiers_size = 3 * 48 * sizeof (LiteMath::int4);
    VkDeviceSize rotation_add_size = 2304 * sizeof (uint32_t);
    VkDeviceSize subtree_size = (1LL << (3 * subtree_root_level)) * sizeof (SComTreeStackElement);

    if (nodes_size == 0) {
        throw std::runtime_error ("SComTree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (5 + max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (5 + max_frames_in_flight);

    this->subtree_root_buffers.clear ();

    buffers [0] = vk_utils::createBuffer (device, header_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    buffers [1] = vk_utils::createBuffer (device, nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [1]);
    buffers [2] = vk_utils::createBuffer (device, bricks_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2]);
    buffers [3] = vk_utils::createBuffer (device, rotation_modifiers_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [3]);
    buffers [4] = vk_utils::createBuffer (device, rotation_add_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [4]);

    this->header_buffer = buffers [0];
    this->nodes_buffer = buffers [1];
    this->bricks_buffer = buffers [2];
    this->rotation_modifiers_buffer = buffers [3];
    this->rotation_add_buffer = buffers [4];

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [5 + i] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [5 + i]);
        this->subtree_root_buffers.push_back (buffers [i + 1]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (this->header_buffer, 0, &scomtree.header, header_size);
    copy_helper->UpdateBuffer (this->nodes_buffer, 0, scomtree.nodes.data (), nodes_size);
    copy_helper->UpdateBuffer (this->bricks_buffer, 0, scomtree.bricks.data (), bricks_size);
    copy_helper->UpdateBuffer (this->rotation_modifiers_buffer, 0, rotation_modifiers, rotation_modifiers_size);
    copy_helper->UpdateBuffer (this->rotation_add_buffer, 0, rotation_add, rotation_add_size);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->header_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (1, this->nodes_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (2, this->bricks_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (3, this->rotation_modifiers_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (4, this->rotation_add_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (5, this->subtree_root_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

SComTreeTreeDescriptorSetInfo::~SComTreeTreeDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    if (this->header_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->header_buffer, nullptr);
        this->header_buffer = VK_NULL_HANDLE;
    }

    if (this->nodes_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->nodes_buffer, nullptr);
        this->nodes_buffer = VK_NULL_HANDLE;
    }

    if (this->bricks_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->bricks_buffer, nullptr);
        this->bricks_buffer = VK_NULL_HANDLE;
    }

    if (this->rotation_modifiers_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->rotation_modifiers_buffer, nullptr);
        this->rotation_modifiers_buffer = VK_NULL_HANDLE;
    }

    if (this->rotation_add_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->rotation_add_buffer, nullptr);
        this->rotation_add_buffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < this->subtree_root_buffers.size (); ++i) {
        if (this->subtree_root_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, this->subtree_root_buffers [i], nullptr);
            this->subtree_root_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (this->device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

std::vector <NodeContext> get_scomtree_subtrees_payloads (const SComTree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

std::vector <NodeContext> get_scomtree_subtrees_payloads_parallel (const SComTree& /*scene*/, int /*max_level_to_descend*/) {
    // TODO
    assert (false);
    return {};
}

int get_scomtree_max_depth (const SComTree& /*scene*/) {
    // TODO
    assert (false);
    return 0;
}

}

