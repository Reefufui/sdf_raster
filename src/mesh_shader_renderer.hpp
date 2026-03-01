#pragma once

#include <memory>
#include <vector>
#include <string>

#include <GLFW/glfw3.h>

#include "vk_descriptor_sets.h"

#include "shader_common.hpp"

#include "application.hpp"
#include "camera.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "sdf_octree.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class MeshShaderRenderer  : public Renderer {
public:
    explicit MeshShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context);
    ~MeshShaderRenderer ();

    void init (SdfOctree&& a_sdf_octree) override;
    void render (const Application::Settings& settings) override;
    void shutdown () override;
    void toggle_frustum_buffer (Camera& camera) override;
    const Stats& get_stats () override;

private:
    void init_mesh_shading_pipeline ();

    void update_push_constants (const Camera& a_camera);

    std::shared_ptr <VulkanContext> context {nullptr};

    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};
    SdfOctreeDescriptorSetInfo sdf_octree_ds {};
    ActiveLeafsDescriptorSetInfo active_leafs_ds {};
    MarchingCubesLookupTableDescriptorSetInfo marching_cubes_lookup_table_ds {};

    VkRenderPass render_pass {VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout {VK_NULL_HANDLE};
    VkPipeline pipeline {VK_NULL_HANDLE};

    int width {};
    int height {};
    SdfOctree sdf_octree {};
    std::vector <NodeContext> subtrees {};

	VkDeviceSize active_leafs_size;
    PushConstantsData push_constants;

    bool initialized {false};
};

} // namespace sdf_raster

