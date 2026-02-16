#pragma once

#include <memory>
#include <vector>
#include <string>
#include <iostream>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "sdf_octree.hpp"
#include "shaders/common.h"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"

namespace sdf_raster {

class ComputeShaderRenderer : public Renderer {
public:
    explicit ComputeShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context);
    ~ComputeShaderRenderer ();

    void init (int a_width, int a_height, SdfOctree&& a_sdf_octree, size_t a_max_vertices_count) override;
    void render (const Camera& a_camera) override;
    void resize (int a_width, int a_height) override;
    void shutdown () override;
    void update_push_constants (const Camera& a_camera) override;

private:
    void init_descriptor_sets ();
    void init_compute_active_leafs_pipeline ();
    void init_compute_prefix_sum_pass1_pipeline ();
    void init_compute_prefix_sum_pass2_pipeline ();
    void init_compute_prefix_sum_pass3_pipeline ();
    void init_compute_geometry_pipeline ();
    void init_graphics_shading_pipeline ();

    void reset_active_leafs_counter (VkCommandBuffer cmd_buff, size_t current_frame);
    void compute_active_leafs (VkCommandBuffer cmd_buff, size_t current_frame);
    void active_leafs_barrier (VkCommandBuffer cmd_buff, size_t current_frame);
    void prefix_sum_pass1 (VkCommandBuffer cmd_buff, size_t current_frame);
    void prefix_sum_pass2 (VkCommandBuffer cmd_buff, size_t current_frame);
    void prefix_sum_pass3 (VkCommandBuffer cmd_buff, size_t current_frame);
    void compute_geometry (VkCommandBuffer cmd_buff, size_t current_frame);
    void geometry_barrier (VkCommandBuffer cmd_buff, size_t current_frame);
    void draw_geometry (VkCommandBuffer cmd_buff, size_t current_frame);

    std::shared_ptr <VulkanContext> context {nullptr};

    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};
    SdfOctreeDescriptorSetInfo sdf_octree_ds {};
    MeshDescriptorSetInfo mesh_ds {};
    MarchingCubesLookupTableDescriptorSetInfo marching_cubes_lookup_table_ds {};
    ActiveLeafsDescriptorSetInfo active_leafs_ds {};
    DrawIndexedIndirectCommandDescriptorSetInfo draw_indexed_indirect_command_ds {};

    VkRenderPass render_pass {VK_NULL_HANDLE};
    VkPipelineLayout graphics_pipeline_layout {VK_NULL_HANDLE};
    VkPipeline graphics_pipeline {VK_NULL_HANDLE};

    VkPipeline compute_active_leafs_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_geometry_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass1_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass2_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass3_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_active_leafs_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_geometry_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass1_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass2_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass3_pipeline_layout {VK_NULL_HANDLE};

    int width {};
    int height {};
    SdfOctree sdf_octree {};
    std::vector <NodeContext> subtrees {};

    PushConstantsData push_constants;

    bool initialized {false};
};

} // namespace sdf_raster

