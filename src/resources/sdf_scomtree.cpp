// resources/sdf_scomtree.cpp
#include "resources/sdf_scomtree.hpp"

#include "logger.hpp"
#include "scenes/scomtree/defs.hpp"
#include "scenes/scomtree/rotation_lookup_tables.hpp"

#include <vk_buffers.h>

#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

namespace sdf_raster {

SComTreeTreeDescriptorSetInfo::SComTreeTreeDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , VkShaderStageFlags shader_stage_flags)
    : device (device)
    , physical_device (physical_device)
    , copy_helper (copy_helper)
    , shader_stage_flags (shader_stage_flags) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    VkDescriptorSetLayoutBinding bindings [6] = {};
    bindings [0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, this->shader_stage_flags, nullptr };
    bindings [1] = { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, this->shader_stage_flags, nullptr };
    bindings [2] = { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, this->shader_stage_flags, nullptr };
    bindings [3] = { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, this->shader_stage_flags, nullptr };
    bindings [4] = { 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, this->shader_stage_flags, nullptr };
    bindings [5] = { 5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, this->shader_stage_flags, nullptr };

    VkDescriptorSetLayoutCreateInfo layout_info {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 6;
    layout_info.pBindings = bindings;
    VK_CHECK_RESULT (vkCreateDescriptorSetLayout (this->device, &layout_info, nullptr, &this->descriptor_set_layout));

    VkDeviceSize rotation_modifiers_size = 3 * 48 * sizeof (LiteMath::int4);
    VkDeviceSize rotation_add_size = 2304 * sizeof (uint32_t);

    this->rotation_modifiers_buffer = vk_utils::createBuffer (this->device, rotation_modifiers_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);
    this->rotation_add_buffer = vk_utils::createBuffer (this->device, rotation_add_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, nullptr);

    this->rotation_memory = vk_utils::allocateAndBindWithPadding (this->device, this->physical_device, { this->rotation_modifiers_buffer, this->rotation_add_buffer });

    this->copy_helper->UpdateBuffer (this->rotation_modifiers_buffer, 0, rotation_modifiers, rotation_modifiers_size);
    this->copy_helper->UpdateBuffer (this->rotation_add_buffer, 0, rotation_add, rotation_add_size);
}

void SComTreeTreeDescriptorSetInfo::add_scene (const std::string& id, std::shared_ptr <SComTreeModel> scene) {
    SceneEntry entry {};
    entry.scene = scene;

    const SComTree& model_data = scene->get_octree_data ();
    const ModelState& model_state = scene->get_state ();

    VkDeviceSize header_size = sizeof (SComTreeHeader);
    VkDeviceSize nodes_size = model_data.nodes.size () * sizeof (uint32_t);
    VkDeviceSize bricks_size = model_data.bricks.size () * sizeof (uint32_t);
    VkDeviceSize subtree_size = (1LL << (3 * model_state.cpu_traversed)) * sizeof (SComTreeStackElement);

    if (nodes_size == 0) {
        throw std::runtime_error ("SComTree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (4);
    std::vector <VkMemoryRequirements> mem_reqs (4);

    buffers [0] = vk_utils::createBuffer (this->device, header_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &mem_reqs [0]);
    buffers [1] = vk_utils::createBuffer (this->device, nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [1]);
    buffers [2] = vk_utils::createBuffer (this->device, bricks_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2]);
    buffers [3] = vk_utils::createBuffer (this->device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [3]);

    entry.resources.header_buffer = buffers [0];
    entry.resources.nodes_buffer = buffers [1];
    entry.resources.bricks_buffer = buffers [2];
    entry.resources.subtree_root_buffer = buffers [3];

    entry.resources.memory = vk_utils::allocateAndBindWithPadding (this->device, this->physical_device, buffers);

    this->copy_helper->UpdateBuffer (entry.resources.header_buffer, 0, &model_data.header, header_size);
    this->copy_helper->UpdateBuffer (entry.resources.nodes_buffer, 0, model_data.nodes.data (), nodes_size);
    this->copy_helper->UpdateBuffer (entry.resources.bricks_buffer, 0, model_data.bricks.data (), bricks_size);

    {
        VkDescriptorPoolSize pool_sizes [2] = {};
        pool_sizes [0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
        pool_sizes [1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 };

        VkDescriptorPoolCreateInfo pool_info {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = 0;
        pool_info.maxSets = 1;
        pool_info.poolSizeCount = 2;
        pool_info.pPoolSizes = pool_sizes;
        VK_CHECK_RESULT (vkCreateDescriptorPool (this->device, &pool_info, nullptr, &entry.resources.descriptor_pool));
    }

    {
        VkDescriptorSetAllocateInfo alloc_info {};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = entry.resources.descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &this->descriptor_set_layout;
        VK_CHECK_RESULT (vkAllocateDescriptorSets (this->device, &alloc_info, &entry.resources.descriptor_set));
    }

    {
        VkDescriptorBufferInfo header_info = { entry.resources.header_buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo nodes_info = { entry.resources.nodes_buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo bricks_info = { entry.resources.bricks_buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo rotation_modifiers_info = { this->rotation_modifiers_buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo rotation_add_info = { this->rotation_add_buffer, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo subtree_info = { entry.resources.subtree_root_buffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet writes [6] = {};
        writes [0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [0].dstSet = entry.resources.descriptor_set;
        writes [0].dstBinding = 0;
        writes [0].descriptorCount = 1;
        writes [0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes [0].pBufferInfo = &header_info;

        writes [1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [1].dstSet = entry.resources.descriptor_set;
        writes [1].dstBinding = 1;
        writes [1].descriptorCount = 1;
        writes [1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes [1].pBufferInfo = &nodes_info;

        writes [2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [2].dstSet = entry.resources.descriptor_set;
        writes [2].dstBinding = 2;
        writes [2].descriptorCount = 1;
        writes [2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes [2].pBufferInfo = &bricks_info;

        writes [3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [3].dstSet = entry.resources.descriptor_set;
        writes [3].dstBinding = 3;
        writes [3].descriptorCount = 1;
        writes [3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes [3].pBufferInfo = &rotation_modifiers_info;

        writes [4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [4].dstSet = entry.resources.descriptor_set;
        writes [4].dstBinding = 4;
        writes [4].descriptorCount = 1;
        writes [4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes [4].pBufferInfo = &rotation_add_info;

        writes [5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes [5].dstSet = entry.resources.descriptor_set;
        writes [5].dstBinding = 5;
        writes [5].descriptorCount = 1;
        writes [5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes [5].pBufferInfo = &subtree_info;

        vkUpdateDescriptorSets (this->device, 6, writes, 0, nullptr);
    }

    {
        VkMemoryRequirements mem_req;
        entry.resources.subtree_roots_staging_buffer = vk_utils::createBuffer (this->device, subtree_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &mem_req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mem_req.size;
        allocInfo.memoryTypeIndex = vk_utils::findMemoryType (mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, this->physical_device);

        VK_CHECK_RESULT (vkAllocateMemory (this->device, &allocInfo, nullptr, &entry.resources.staging_buffer_memory));
        vkBindBufferMemory (this->device, entry.resources.subtree_roots_staging_buffer, entry.resources.staging_buffer_memory, 0);
        VK_CHECK_RESULT (vkMapMemory (this->device, entry.resources.staging_buffer_memory, 0, subtree_size, 0, &entry.resources.subtrees_memory_mapped));
    }

    this->scenes [id] = std::move (entry);
}

SComTreeTreeDescriptorSetInfo::~SComTreeTreeDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    for (auto& [id, entry] : this->scenes) {
        auto& r = entry.resources;

        if (r.subtree_roots_staging_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, r.subtree_roots_staging_buffer, nullptr);
        }

        if (r.staging_buffer_memory != VK_NULL_HANDLE) {
            vkUnmapMemory (this->device, r.staging_buffer_memory);
            vkFreeMemory (this->device, r.staging_buffer_memory, nullptr);
        }

        if (r.descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool (this->device, r.descriptor_pool, nullptr);
        }

        if (r.header_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, r.header_buffer, nullptr);
        }

        if (r.nodes_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, r.nodes_buffer, nullptr);
        }

        if (r.bricks_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, r.bricks_buffer, nullptr);
        }

        if (r.subtree_root_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer (this->device, r.subtree_root_buffer, nullptr);
        }

        if (r.memory != VK_NULL_HANDLE) {
            vkFreeMemory (this->device, r.memory, nullptr);
        }
    }
    this->scenes.clear ();

    if (this->rotation_modifiers_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->rotation_modifiers_buffer, nullptr);
        this->rotation_modifiers_buffer = VK_NULL_HANDLE;
    }

    if (this->rotation_add_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->device, this->rotation_add_buffer, nullptr);
        this->rotation_add_buffer = VK_NULL_HANDLE;
    }

    if (this->rotation_memory != VK_NULL_HANDLE) {
        vkFreeMemory (this->device, this->rotation_memory, nullptr);
        this->rotation_memory = VK_NULL_HANDLE;
    }

    if (this->descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout (this->device, this->descriptor_set_layout, nullptr);
        this->descriptor_set_layout = VK_NULL_HANDLE;
    }
}

void SComTreeTreeDescriptorSetInfo::update_subtree_root_buffer (const std::string& id, const FrustumGeometry& frustum) {
    auto& entry = this->scenes.at (id);
    assert (entry.scene);
    auto visible_subtrees = entry.scene->collect_visible_subtrees (frustum);
    entry.resources.subtree_count = visible_subtrees.size ();
    if (entry.resources.subtree_count) {
        memcpy (entry.resources.subtrees_memory_mapped, visible_subtrees.data (), entry.resources.subtree_count * sizeof (SComTreeStackElement));
    }
}

} // sdf_raster
