#pragma once

#include <memory>
#include <vector>
#include <string>
#include <iostream>

#include "GLFW/glfw3.h"

#include "camera.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "mesh.hpp"
#include "sdf_octree.hpp"
#include "shaders/common.h"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class MeshShaderRenderer {
public:
    explicit MeshShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context);
    ~MeshShaderRenderer ();

    void init (int a_width, int a_height, SdfOctree&& a_sdf_octree);
    void render (const Camera& a_camera);
    void resize (int a_width, int a_height);
    void shutdown ();
    void update_push_constants (const Camera& a_camera);

private:
    void init_mesh_shading_pipeline ();

    std::shared_ptr <VulkanContext> context {nullptr};

    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};
    SdfOctreeDescriptorSetInfo sdf_octree_ds {};
    MarchingCubesLookupTableDescriptorSetInfo marching_cubes_lookup_table_ds {};

    VkRenderPass render_pass {VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout {VK_NULL_HANDLE};
    VkPipeline pipeline {VK_NULL_HANDLE};

    int width {};
    int height {};
    SdfOctree sdf_octree {};

    PushConstantsData push_constants;

    bool initialized {false};
};

} // namespace sdf_raster

