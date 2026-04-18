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

// TODO: move to scenes/scomtree
void load_scomtree (SComTree& scene, const std::filesystem::path& path);
float sample_sdf (const SComTree& scene, const LiteMath::float3& p);

class SComTreeTreeDescriptorSetInfo {
public:
    SComTreeTreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , const SComTree& scomtree
        , size_t subtree_root_level
        , size_t max_frames_in_flight);
    ~SComTreeTreeDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const { return this->descriptor_sets [fif_index]; }
    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

private:
    VkDevice device = VK_NULL_HANDLE;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker;
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    VkBuffer header_buffer = VK_NULL_HANDLE;
    VkBuffer nodes_buffer = VK_NULL_HANDLE;
    VkBuffer bricks_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_modifiers_buffer = VK_NULL_HANDLE;
    VkBuffer rotation_add_buffer = VK_NULL_HANDLE;
    std::vector <VkBuffer> subtree_root_buffers;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

// TODO: move to scenes/scomtree
std::vector <NodeContext> get_scomtree_subtrees_payloads (const SComTree& scene, int max_level_to_descend);
std::vector <NodeContext> get_scomtree_subtrees_payloads_parallel (const SComTree& scene, int max_level_to_descend);
int get_scomtree_max_depth (const SComTree& scene);

}

