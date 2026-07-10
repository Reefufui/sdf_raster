// resources/sdf_scomtree.hpp

#pragma once

#include "model_resource.hpp"

#include "scenes/scomtree/scomtree.hpp"
#include "shader_common.hpp"

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_utils.h>

#include <filesystem>
#include <memory>
#include <string>

namespace sdf_raster {

class SComTreeTreeDescriptorSetInfo : public ModelResource {
public:
    SComTreeTreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkDescriptorSetLayout layout
        , VkBuffer rotation_modifiers_buffer
        , VkBuffer rotation_add_buffer
        , std::shared_ptr <SComTreeModel> scene);
    ~SComTreeTreeDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set () const { return this->descriptor_set; }
    size_t get_subtree_count () const { return this->subtree_count; }
    VkBuffer get_staging_buffer () const { return this->subtree_roots_staging_buffer; }
    VkBuffer get_subtree_root_buffer () const { return this->subtree_root_buffer; }

    void update_subtree_root_buffer (const FrustumGeometry& frustum);

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::shared_ptr <SComTreeModel> scene_;

    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;

    VkBuffer header_buffer = VK_NULL_HANDLE;
    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    VkBuffer bricks_buffer = VK_NULL_HANDLE;
    VkBuffer subtree_root_buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    size_t subtree_count {};

    VkBuffer subtree_roots_staging_buffer = VK_NULL_HANDLE;
    VkDeviceMemory staging_buffer_memory = VK_NULL_HANDLE;
    void* subtrees_memory_mapped = nullptr;
};

class SComTreeTreeDescriptorSetInfoFabric {
public:
    SComTreeTreeDescriptorSetInfoFabric (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags);
    ~SComTreeTreeDescriptorSetInfoFabric ();

    std::unique_ptr <SComTreeTreeDescriptorSetInfo> create_scene (const std::string& id, std::shared_ptr <SComTreeModel> scene);

    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::shared_ptr <vk_utils::ICopyEngine> copy_helper {};
    VkShaderStageFlags shader_stage_flags = 0;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer rotation_modifiers_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_add_buffer = VK_NULL_HANDLE;
    VkDeviceMemory rotation_memory = VK_NULL_HANDLE;
};

} // sdf_raster
