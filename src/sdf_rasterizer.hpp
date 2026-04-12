#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

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
#include "shader_common.hpp"
#include "state.hpp"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"

#include <string_view>

namespace sdf_raster {

class SDFRasterizer : public Renderer {
public:
    explicit SDFRasterizer (std::shared_ptr <VulkanContext> vulkan_context);
    ~SDFRasterizer ();

    void init () override;
    void update (uint32_t frame_index, Settings& settings, SceneState& scene_state) override;
    void render (VkCommandBuffer cmd_buff) override;
    void shutdown (Settings& settings) override;
    void process_commands (std::queue <RenderCommand>& commands, std::mutex& mutex) override;
    void recreate_scene_resources (Scene* scene);
    void release_scene_resources ();
    const Stats& get_stats () override;

private:
    void init_push_constants ();
    void init_descriptor_sets ();
    void init_compute_hz_buffer_pipeline ();
    void init_compute_prepare_indirect_pipeline ();
    void init_compute_active_leafs_pipeline ();
    void init_compute_prefix_sum_pass1_pipeline ();
    void init_compute_prefix_sum_pass2_pipeline ();
    void init_compute_prefix_sum_pass3_pipeline ();
    void init_compute_geometry_pipeline ();
    void init_graphics_identity_pipeline ();
    void init_graphics_viewproj_pipeline ();
    void init_graphics_lighting_pipeline ();
    void init_graphics_gbuffer_pipeline ();
    void init_mesh_shading_pipeline ();

    void register_resizable ();

    void init_graphics_frustum_pipeline ();

    void init_subtree_roots_staging_buffer ();
    void cleanup_subtree_roots_staging_buffer ();

    void update_frustum_buffer (const Camera& camera);
    void reset_active_leafs_counter (VkCommandBuffer cmd_buff);
    void clear_geometry (VkCommandBuffer cmd_buff);
    void compute_hz_buffer (VkCommandBuffer cmd_buff);
    void compute_active_leafs (VkCommandBuffer cmd_buff);
    void hz_buffer_barrier (VkCommandBuffer cmd_buff);
    void prepare_draw_indirect (VkCommandBuffer cmd_buff);
    void prepare_indirect (VkCommandBuffer cmd_buff, uint32_t workgroup_size);
    void prefix_sum_pass1 (VkCommandBuffer cmd_buff);
    void prefix_sum_pass2 (VkCommandBuffer cmd_buff);
    void prefix_sum_pass3 (VkCommandBuffer cmd_buff);
    void compute_geometry (VkCommandBuffer cmd_buff);
    void geometry_barrier (VkCommandBuffer cmd_buff);
    void draw_geometry (VkCommandBuffer cmd_buff);
    void draw_frustum (VkCommandBuffer cmd_buff);
    void copy_depth (VkCommandBuffer cmd_buff);
    void copy_subtrees (VkCommandBuffer cmd_buff);

    void sync_draw_method (SceneState& scene_state);

    std::shared_ptr <VulkanContext> context {nullptr};

    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};
    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker_for_resizable {nullptr};
    SdfOctreeDescriptorSetInfo sdf_octree_ds {};
    MeshDescriptorSetInfo mesh_ds {};
    MarchingCubesLookupTableDescriptorSetInfo marching_cubes_lookup_table_ds {};
    ActiveLeafsDescriptorSetInfo active_leafs_ds {};
    DrawIndexedIndirectCommandDescriptorSetInfo draw_indexed_indirect_command_ds {};
    HZBufferDescriptorSetInfo hz_buffer_ds {};
    FrustumDescriptorSetInfo frustum_ds {};
    IndirectDispatchDescriptorSetInfo indirect_dispatch_ds {};
    LODDescriptorSetInfo lod_ds {};

    VkPipelineLayout mesh_pipeline_layout {VK_NULL_HANDLE};
    VkPipeline mesh_pipeline {VK_NULL_HANDLE};

    VkPipeline graphics_frustum_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_gbuffer_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_identity_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_lighting_pipeline {VK_NULL_HANDLE};
    VkPipeline graphics_viewproj_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout graphics_frustum_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_gbuffer_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_identity_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_lighting_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout graphics_viewproj_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline compute_hz_buffer_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_active_leafs_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prepare_indirect_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_geometry_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass1_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass2_pipeline {VK_NULL_HANDLE};
    VkPipeline compute_prefix_sum_pass3_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_hz_buffer_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_active_leafs_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prepare_indirect_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_geometry_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass1_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass2_pipeline_layout {VK_NULL_HANDLE};
    VkPipelineLayout compute_prefix_sum_pass3_pipeline_layout {VK_NULL_HANDLE};

    std::unique_ptr <DeferredShading> deferred_shading;

    std::vector <NodeContext> subtrees {};
    std::vector <NodeContext> visible_subtrees {};
    VkBuffer subtrees_buffer {VK_NULL_HANDLE};
    VkDeviceMemory subtrees_memory {VK_NULL_HANDLE};
    void* subtrees_memory_mapped = nullptr;

    void raster_explicit (VkCommandBuffer cmd_buff);
    void raster_explicit_deferred (VkCommandBuffer cmd_buff);
    void raster_implicit_via_compute_shading (VkCommandBuffer cmd_buff);
    void raster_implicit_via_mesh_shading (VkCommandBuffer cmd_buff);
    using RenderMethodPtr = void (SDFRasterizer::*)(VkCommandBuffer);
    RenderMethodPtr draw = &SDFRasterizer::raster_explicit;

    struct MethodTrait {
        DrawMethod method;
        RenderMethodPtr ptr;
        std::string_view name;
        bool needs_mesh_shading;
    };

    static inline constexpr std::array <MethodTrait, 5> draw_strategies = {{
          { DrawMethod::None, &SDFRasterizer::raster_explicit, "None (Idle)", false}
        , { DrawMethod::Explicit, &SDFRasterizer::raster_explicit, "Explicit", false}
        , { DrawMethod::ExplicitDeferred, &SDFRasterizer::raster_explicit_deferred, "Explicit Deferred", false}
        , { DrawMethod::ImplicitCompute, &SDFRasterizer::raster_implicit_via_compute_shading, "Compute", false}
        , { DrawMethod::ImplicitMesh, &SDFRasterizer::raster_implicit_via_mesh_shading, "Mesh", true }
    }};

    DrawMethod last_applied_method = DrawMethod::None;

    FrustumGeometry frustum {};
    std::unique_ptr <FrustumDrawBuffer> frustum_draw_buffer {nullptr};

    PushConstantsData push_constants {};
    Stats stats {};
    int cpu_traversed {};
    std::string scene_name;
    LiteMath::float4 clear_color {0.25f, 0.25f, 0.25f, 1.0f};

    uint32_t explicit_index_count {};

    uint32_t frame_index {0};
    bool initialized {false};
};

} // namespace sdf_raster

