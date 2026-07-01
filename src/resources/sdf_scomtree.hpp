// resources/sdf_scomtree.hpp

#pragma once

#include "model_resource.hpp"

#include "scenes/scomtree/scomtree.hpp"
#include "shader_common.hpp"

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_utils.h>

#include <filesystem>
#include <string>
#include <unordered_map>

namespace sdf_raster {

class SComTreeTreeDescriptorSetInfo : public ModelResource {
public:
    struct SceneResources {
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

    struct SceneEntry {
        std::shared_ptr <SComTreeModel> scene {};
        SceneResources resources {};
    };

    SComTreeTreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags);
    ~SComTreeTreeDescriptorSetInfo ();

    void add_scene (const std::string& id, std::shared_ptr <SComTreeModel> scene);

    const SceneEntry& get_scene (const std::string& id) const { return this->scenes.at (id); }

    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    void update_subtree_root_buffer (const std::string& id, const FrustumGeometry& frustum);

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    std::shared_ptr <vk_utils::ICopyEngine> copy_helper {};
    VkShaderStageFlags shader_stage_flags = 0;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::unordered_map <std::string, SceneEntry> scenes {};

    VkBuffer rotation_modifiers_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_add_buffer = VK_NULL_HANDLE;
    VkDeviceMemory rotation_memory = VK_NULL_HANDLE;
};

} // sdf_raster
