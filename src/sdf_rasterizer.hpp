#pragma once

#include "active_leafs.hpp"
#include "camera.hpp"
#include "deferred_shading.hpp"
#include "indirect_dispatch.hpp"
#include "lod.hpp"
#include "marching_cubes_lookup_table.hpp"
#include "mesh.hpp"
#include "occlusion_culling.hpp"
#include "renderer.hpp"
#include "scenes/scene.hpp"
#include "sdf_octree.hpp"
#include "sdf_scomtree.hpp"
#include "shader_common.hpp"
#include "state.hpp"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"

#include <GLFW/glfw3.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sdf_raster {

class SDFRasterizer : public Renderer {
public:
    explicit SDFRasterizer (std::shared_ptr <VulkanContext> vulkan_context);
    ~SDFRasterizer ();

    void init () override;
    void update (uint32_t frame_index, Settings& settings) override;
    void render (VkCommandBuffer cmd_buff) override;
    void shutdown () override;
    void process_commands (std::queue <RenderCommand>& commands, std::mutex& mutex) override;
    void apply_scene_config (std::shared_ptr <Scene> scene);
    const Stats& get_stats () override;

private:
    void init_push_constants ();
    void release_render_resources ();

    void init_compute_hz_buffer_pipeline ();
    void init_compute_prepare_indirect_pipeline ();
    void init_traverse_octree_pipeline ();
    void init_traverse_scomtree_pipeline ();
    void init_marching_cubes_octree_pipeline ();
    void init_marching_cubes_scomtree_pipeline ();
    void init_forward_rendering_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path, VkFrontFace front_face);
    // TODO: init_deferred_rendering_pipelines
    void init_graphics_lighting_pipeline ();
    void init_graphics_gbuffer_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path);
    // TODO: ^^^^
    void init_mesh_shading_octree_pipeline ();
    void init_mesh_shading_scomtree_pipeline ();

    void register_resizable ();

    void init_frustum_demo_pipeline ();

    void clear_geometry (VkCommandBuffer cmd_buff);
    void compute_hz_buffer (VkCommandBuffer cmd_buff);
    void copy_depth (VkCommandBuffer cmd_buff);
    void copy_subtrees (VkCommandBuffer cmd_buff);
    void deferred_rendering (VkCommandBuffer cmd_buff);
    void draw_frustum_demo (VkCommandBuffer cmd_buff);
    void forward_rendering (VkCommandBuffer cmd_buff);
    void geometry_barrier (VkCommandBuffer cmd_buff);
    void hz_buffer_barrier (VkCommandBuffer cmd_buff);
    void marching_cubes_octree (VkCommandBuffer cmd_buff);
    void marching_cubes_scomtree (VkCommandBuffer cmd_buff);
    void prepare_draw_indirect (VkCommandBuffer cmd_buff);
    void prepare_indirect (VkCommandBuffer cmd_buff, uint32_t workgroup_size);
    void reset_active_leafs_counter (VkCommandBuffer cmd_buff);
    void traverse_octree (VkCommandBuffer cmd_buff);
    void traverse_scomtree (VkCommandBuffer cmd_buff);
    void update_frustum_buffer (const Camera& camera);

    std::shared_ptr <VulkanContext> context {nullptr};

    std::unique_ptr <SComTreeTreeDescriptorSetInfo> sdf_scomtree_ds {};
    std::unique_ptr <SdfOctreeDescriptorSetInfo> sdf_octree_ds {};
    std::unique_ptr <MeshDescriptorSetInfo> mesh_ds {};
    std::unique_ptr <MarchingCubesLookupTableDescriptorSetInfo> marching_cubes_lookup_table_ds {};
    std::unique_ptr <ActiveLeafsDescriptorSetInfo> active_leafs_ds {};
    std::unique_ptr <IndirectDescriptorSetInfo> draw_indexed_indirect_command_ds {};
    std::unique_ptr <HZBufferDescriptorSetInfo> hz_buffer_ds {};
    std::unique_ptr <FrustumDescriptorSetInfo> frustum_ds {};
    std::unique_ptr <IndirectDescriptorSetInfo> indirect_dispatch_ds {};
    std::unique_ptr <LODDescriptorSetInfo> lod_ds {};

    VkPipeline mesh_shading_octree_pipeline {VK_NULL_HANDLE};
    VkPipeline mesh_shading_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout mesh_shading_octree_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout mesh_shading_scomtree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline forward_rendering_pipeline {VK_NULL_HANDLE};
    VkPipeline frustum_demo_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_gbuffer_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_lighting_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout forward_rendering_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout frustum_demo_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_gbuffer_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_lighting_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline compute_hz_buffer_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prepare_indirect_pipeline {VK_NULL_HANDLE};
    VkPipeline marching_cubes_octree_pipeline {VK_NULL_HANDLE};
    VkPipeline marching_cubes_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipeline traverse_octree_pipeline {VK_NULL_HANDLE};
    VkPipeline traverse_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_hz_buffer_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prepare_indirect_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout marching_cubes_octree_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout marching_cubes_scomtree_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout traverse_octree_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout traverse_scomtree_pipeline_layout {VK_NULL_HANDLE};

    std::shared_ptr <Scene> current_scene {};

    std::unique_ptr <DeferredShading> deferred_shading {};

    void raster_octree_via_compute_shading (VkCommandBuffer cmd_buff);
    void raster_scomtree_via_compute_shading (VkCommandBuffer cmd_buff);
    void raster_octree_via_mesh_shading (VkCommandBuffer cmd_buff);
    void raster_scomtree_via_mesh_shading (VkCommandBuffer cmd_buff);
    using RenderMethodPtr = void (SDFRasterizer::*)(VkCommandBuffer);
    RenderMethodPtr draw = &SDFRasterizer::forward_rendering;

    struct MethodTrait {
        DrawMethod method;
        RenderMethodPtr ptr;
        bool needs_mesh_shading;
    };

    static inline constexpr std::array <MethodTrait, 9> draw_strategies = {{
          { DrawMethod::None, &SDFRasterizer::forward_rendering, false}
        , { DrawMethod::Explicit, &SDFRasterizer::forward_rendering, false}
        , { DrawMethod::ExplicitDeferred, &SDFRasterizer::deferred_rendering, false}
        , { DrawMethod::OctreeCompute, &SDFRasterizer::raster_octree_via_compute_shading, false}
        , { DrawMethod::OctreeMesh, &SDFRasterizer::raster_octree_via_mesh_shading, true }
        , { DrawMethod::SComTreeCompute, &SDFRasterizer::raster_scomtree_via_compute_shading, false }
        , { DrawMethod::SComTreeComputeDeferred, &SDFRasterizer::raster_scomtree_via_compute_shading, false }
        , { DrawMethod::SComTreeMesh, &SDFRasterizer::raster_scomtree_via_mesh_shading, true }
        , { DrawMethod::SComTreeMeshDeferred, &SDFRasterizer::raster_scomtree_via_mesh_shading, true }
    }};

    FrustumGeometry frustum {};
    std::unique_ptr <FrustumDrawBuffer> frustum_draw_buffer {nullptr};

    PushConstantsData push_constants {};
    Stats stats {};
    int cpu_traversed {};
    LiteMath::float4 clear_color {0.25f, 0.25f, 0.25f, 1.0f};

    uint32_t explicit_index_count {};

    uint32_t frame_index {0};
};

} // namespace sdf_raster

