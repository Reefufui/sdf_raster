#pragma once

#include <memory>
#include <vector>
#include <string>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "mesh.hpp"
#include "occlusion_culling.hpp"
#include "renderer.hpp"
#include "sdf_octree.hpp"
#include "settings.hpp"
#include "shader_common.hpp"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"

namespace sdf_raster {

class ComputeShaderRenderer : public Renderer {
public:
    explicit ComputeShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context);
    ~ComputeShaderRenderer ();

    void init (SdfOctree&& a_sdf_octree) override;
    void render (const Settings& settings) override;
    void shutdown () override;

private:
    void init_push_constants ();
    void init_descriptor_sets ();
    void init_compute_hz_buffer_pipeline ();
    void init_compute_active_leafs_pipeline ();
    void init_compute_prefix_sum_pass1_pipeline ();
    void init_compute_prefix_sum_pass2_pipeline ();
    void init_compute_prefix_sum_pass3_pipeline ();
    void init_compute_geometry_pipeline ();
    void init_graphics_shading_pipeline ();

    void register_resizable ();

    void init_graphics_frustum_pipeline ();
    void toggle_frustum_buffer (Camera& camera) override;

    void update_push_constants (const Settings& settings);
    void update_frustum_buffer (const Camera& camera);
    void reset_active_leafs_counter (VkCommandBuffer cmd_buff);
    void clear_geometry (VkCommandBuffer cmd_buff);
    void compute_hz_buffer (VkCommandBuffer cmd_buff);
    void compute_active_leafs (VkCommandBuffer cmd_buff);
    void hz_buffer_barrier (VkCommandBuffer cmd_buff);
    void active_leafs_barrier (VkCommandBuffer cmd_buff);
    void prefix_sum_pass1 (VkCommandBuffer cmd_buff);
    void prefix_sum_pass2 (VkCommandBuffer cmd_buff);
    void prefix_sum_pass3 (VkCommandBuffer cmd_buff);
    void compute_geometry (VkCommandBuffer cmd_buff);
    void geometry_barrier (VkCommandBuffer cmd_buff);
    void draw_geometry (VkCommandBuffer cmd_buff);
    void draw_frustum (VkCommandBuffer cmd_buff);
    void copy_depth (VkCommandBuffer cmd_buff);

    std::shared_ptr <VulkanContext> context {nullptr};

    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};
    SdfOctreeDescriptorSetInfo sdf_octree_ds {};
    MeshDescriptorSetInfo mesh_ds {};
    MarchingCubesLookupTableDescriptorSetInfo marching_cubes_lookup_table_ds {};
    ActiveLeafsDescriptorSetInfo active_leafs_ds {};
    DrawIndexedIndirectCommandDescriptorSetInfo draw_indexed_indirect_command_ds {};
    HZBufferDescriptorSetInfo hz_buffer_ds {};
    FrustumDescriptorSetInfo frustum_ds {};

    VkPipelineLayout graphics_pipeline_layout {VK_NULL_HANDLE};
    VkPipeline graphics_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout graphics_frustum_pipeline_layout {VK_NULL_HANDLE};
    VkPipeline graphics_frustum_pipeline {VK_NULL_HANDLE};

    VkPipeline compute_hz_buffer_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_active_leafs_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_geometry_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass1_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass2_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass3_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_hz_buffer_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_active_leafs_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_geometry_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass1_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass2_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass3_pipeline_layout {VK_NULL_HANDLE};

    SdfOctree sdf_octree {};
    std::vector <NodeContext> subtrees {};
    std::unique_ptr <FrustumDrawBuffer> frustum_draw_buffer {nullptr};

    PushConstantsData push_constants;

    uint32_t frame_index {0};
    bool initialized {false};
};

} // namespace sdf_raster

