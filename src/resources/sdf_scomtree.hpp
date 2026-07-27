// resources/sdf_scomtree.hpp

#pragma once

#include "scenes/scomtree/scomtree.hpp"
#include "shader_common.hpp"

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_descriptor_sets.h>
#include <vk_utils.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sdf_raster {

class SComTreeTreeDescriptorSetInfo {
public:
    SComTreeTreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , std::shared_ptr <SComTreeScene> scene
        , size_t max_frames_in_flight);
    ~SComTreeTreeDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    void update_subtree_root_buffer (const FrustumGeometry& frustum, uint32_t fif_index);
    void update_subtree_root_buffer_all (uint32_t fif_index);
    VkBuffer get_subtree_root_buffer (uint32_t fif_index) const { return this->subtree_root_buffers [fif_index]; }
    VkBuffer get_subtree_root_staging_buffer (uint32_t fif_index) const { return this->subtree_roots_staging_buffers [fif_index]; }
    size_t get_subtree_count () const { return this->subtree_count; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker;
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::shared_ptr <SComTreeScene> scene {};
    std::vector <VkBuffer> subtree_roots_staging_buffers {};
    std::vector <VkDeviceMemory> staging_buffer_memories {};
    std::vector <void*> subtrees_memory_mapped {};
    size_t subtree_count {};

    VkBuffer header_buffer = VK_NULL_HANDLE;
    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    VkBuffer bricks_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_modifiers_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_add_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

} // sdf_raster