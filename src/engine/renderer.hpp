// engine/renderer.hpp
#pragma once

#include "deferred_shading.hpp"
#include "forward_shading.hpp"
#include "render_target.hpp"

#include "data/mesh.hpp"
#include "resources/active_leafs.hpp"
#include "resources/dummy_ds.hpp"
#include "resources/frustum.hpp"
#include "resources/hz_buffer.hpp"
#include "resources/indirect_dispatch.hpp"
#include "resources/marching_cubes_lookup_table.hpp"
#include "resources/model_resource.hpp"
#include "resources/sdf_octree.hpp"
#include "resources/sdf_scomtree.hpp"
#include "scenes/base/model.hpp"
#include "scenes/scene.hpp"
#include "shader_common.hpp"
#include "state.hpp"
#include "vulkan/vulkan_context.hpp"

#include <vk_descriptor_sets.h>

#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <string_view>
#include <string>
#include <vector>

namespace sdf_raster {

class Renderer {
public:
    Renderer (std::shared_ptr <VulkanContext> context, std::shared_ptr <RenderTarget> render_target);
    ~Renderer ();

    void update (uint32_t frame_index, Settings& settings, float delta_time);
    void render (VkCommandBuffer cmd_buff);
    void render_scene (VkCommandBuffer cmd_buff, const Scene& scene);
    const Stats& get_stats () const;
    void process_commands (std::queue <std::function<void()>>& commands, std::mutex& mutex);
    void apply_model_config (std::shared_ptr <Model> model);
    void setup_pipelines ();
    void cleanup_pipelines ();

    void setup_common_resources ();
    void init_depth_buffer ();

    void resize ();

    void update_local_camera_light (const ModelState& state);

private:
    void cleanup_resources ();
    void init_push_constants ();
    void release_render_resources ();
    void destroy_pipelines ();
    void create_required_pipelines (DrawMethod method);

    void init_compute_hz_buffer_pipeline ();
    void init_traverse_octree_pipeline ();
    void init_traverse_scomtree_pipeline ();
    void init_compute_prepare_indirect_pipeline ();
    void init_marching_cubes_octree_pipeline ();
    void init_marching_cubes_scomtree_pipeline ();
    void init_forward_rendering_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path, VkFrontFace front_face);
    void init_graphics_gbuffer_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path);
    void init_mesh_gbuffer_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path);
    void init_graphics_lighting_pipeline ();
    void init_mesh_shading_octree_pipeline ();
    void init_mesh_shading_scomtree_pipeline ();
    void init_mesh_shading_scomtree_deferred_pipeline ();
    void init_frustum_demo_pipeline ();

    void set_default_viewport_and_scissor (VkCommandBuffer cmd_buff);

    void update_frustum_buffer (const Camera& camera, const LiteMath::float4x4& inv_model);

    void clear_geometry (VkCommandBuffer cmd_buff);
    void copy_forward_rendered_depth (VkCommandBuffer cmd_buff);
    void copy_subtrees (VkCommandBuffer cmd_buff, VkDeviceSize subtrees_size, VkBuffer staging_buffer, VkBuffer buffer);
    void compute_hz_buffer (VkCommandBuffer cmd_buff);
    void reset_active_leafs_counter (VkCommandBuffer cmd_buff);
    void prepare_indirect (VkCommandBuffer cmd_buff, uint32_t workgroup_size);
    void prepare_hzbuffer_after_forward_rendering (VkCommandBuffer cmd_buff);
    void marching_cubes_octree (VkCommandBuffer cmd_buff);
    void marching_cubes_scomtree (VkCommandBuffer cmd_buff, VkDescriptorSet scomtree_ds);
    void geometry_barrier (VkCommandBuffer cmd_buff);
    void traverse_octree (VkCommandBuffer cmd_buff);
    void traverse_scomtree (VkCommandBuffer cmd_buff, uint32_t subtree_root_count, VkDescriptorSet scomtree_ds);

    struct LayoutStageAccess {
        VkImageLayout layout;
        VkPipelineStageFlags stage;
        VkAccessFlags access;
    };
    void hz_buffer_barrier (VkCommandBuffer cmd_buff, LayoutStageAccess src, LayoutStageAccess dst);
    void hz_buffer_barrier (VkCommandBuffer cmd_buff, LayoutStageAccess src_base, LayoutStageAccess dst_base, LayoutStageAccess src_levels, LayoutStageAccess dst_levels);

    const std::unique_ptr <ModelResource>& get_model_resource (const std::string& mesh_id, const std::shared_ptr <Model>& model);

    class RenderMethod {
    public:
        explicit RenderMethod (Renderer& renderer) : r (renderer) {}
        virtual ~RenderMethod () = default;
    
        virtual void begin (VkCommandBuffer cmd_buff, bool& dirty_surface) = 0;
        virtual void draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model) = 0;
        virtual void end (VkCommandBuffer cmd_buff) = 0;
    protected:
        Renderer& r;
    };

    class RasterSComTreeViaComputeShadingForward : public RenderMethod {
    public:
        using RenderMethod::RenderMethod;
        void begin (VkCommandBuffer cmd_buff, bool& dirty_surface) override;
        void draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model) override;
        void end (VkCommandBuffer cmd_buff) override;
    };

    class RasterSComTreeViaComputeShadingDeferred : public RenderMethod {
    public:
        RasterSComTreeViaComputeShadingDeferred (Renderer& renderer);
        ~RasterSComTreeViaComputeShadingDeferred ();

        void begin (VkCommandBuffer cmd_buff, bool& dirty_surface) override;
        void draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model) override;
        void end (VkCommandBuffer cmd_buff) override;
    private:
        std::array <VkClearValue, 4> clear_values {};

        VkRenderPassBeginInfo clear_render_pass_info {};
        VkRenderPassBeginInfo load_render_pass_info {};

        bool dirty_surface {true};
    };

    class RasterMeshForward : public RenderMethod {
    public:
        RasterMeshForward (Renderer& renderer);
        ~RasterMeshForward ();

        void begin (VkCommandBuffer cmd_buff, bool& dirty_surface) override;
        void draw (VkCommandBuffer cmd_buff, const std::unique_ptr <ModelResource>& model) override;
        void end (VkCommandBuffer cmd_buff) override;
    };

    const std::unique_ptr <RenderMethod>& get_render_method (DrawMethod draw_method);

public:
    std::shared_ptr <VulkanContext> context;
    std::shared_ptr <RenderTarget> render_target;

    std::unique_ptr <FrustumDrawBuffer> frustum_draw_buffer;
    std::unique_ptr <HZBufferDescriptorSetInfo> hz_buffer_ds;

    std::unordered_map <std::string, std::unique_ptr <ModelResource>> model_ds_map {};
    std::unordered_map <DrawMethod, std::unique_ptr <RenderMethod>> render_method_map {};

    std::unique_ptr <SdfOctreeDescriptorSetInfo> sdf_octree_ds {};
    std::unique_ptr <SComTreeTreeDescriptorSetInfoFabric> sdf_scomtree_ds {};
    std::unique_ptr <MeshDescriptorSetInfo> mesh_ds {};
    std::unique_ptr <MarchingCubesLookupTableDescriptorSetInfo> marching_cubes_lookup_table_ds {};
    std::unique_ptr <IndirectDescriptorSetInfo> draw_indexed_indirect_command_ds {};
    std::unique_ptr <FrustumDescriptorSetInfo> frustum_ds {};
    std::unique_ptr <DummyDescriptorSetInfo> dummy_ds {};
    std::unique_ptr <IndirectDescriptorSetInfo> indirect_dispatch_ds {};
    std::unique_ptr <ActiveLeafsDescriptorSetInfo> active_leafs_ds {};
    void ensure_resources (DrawMethod method);

    std::shared_ptr <Model> current_model {};

    std::unique_ptr <DeferredShading> deferred_shading {};
    std::unique_ptr <ForwardShading> forward_shading {};
    std::unique_ptr <vk_utils::VulkanImageMem> depth_buffer {};

    void forward_rendering (VkCommandBuffer cmd_buff);
    void prepare_deferred (VkCommandBuffer cmd_buff);
    void deferred_rendering (VkCommandBuffer cmd_buff);
    void calculate_lighting (VkCommandBuffer cmd_buff);
    void raster_octree_via_compute_shading (VkCommandBuffer cmd_buff);
    void raster_scomtree_via_compute_shading (VkCommandBuffer cmd_buff);
    void raster_octree_via_mesh_shading (VkCommandBuffer cmd_buff);
    void raster_scomtree_via_mesh_shading (VkCommandBuffer cmd_buff);
    void raster_scomtree_via_mesh_shading_deferred (VkCommandBuffer cmd_buff);
    void draw_frustum_demo (VkCommandBuffer cmd_buff);

    using RenderMethodPtr = void (Renderer::*)(VkCommandBuffer);
    RenderMethodPtr draw = &Renderer::forward_rendering;

    struct MethodTrait {
        DrawMethod method;
        RenderMethodPtr ptr;
        bool needs_mesh_shading;
    };

    static inline constexpr std::array <MethodTrait, 9> draw_strategies = {{
          { DrawMethod::None, &Renderer::forward_rendering, false}
        , { DrawMethod::Explicit, &Renderer::forward_rendering, false}
        , { DrawMethod::ExplicitDeferred, &Renderer::deferred_rendering, false}
        , { DrawMethod::OctreeCompute, &Renderer::raster_octree_via_compute_shading, false}
        , { DrawMethod::OctreeMesh, &Renderer::raster_octree_via_mesh_shading, true }
        , { DrawMethod::SComTreeCompute, &Renderer::raster_scomtree_via_compute_shading, false }
        , { DrawMethod::SComTreeComputeDeferred, &Renderer::raster_scomtree_via_compute_shading, false }
        , { DrawMethod::SComTreeMesh, &Renderer::raster_scomtree_via_mesh_shading, true }
        , { DrawMethod::SComTreeMeshDeferred, &Renderer::raster_scomtree_via_mesh_shading_deferred, true }
    }};

    FrustumGeometry frustum {};
    LiteMath::float4 frozen_camera_pos {};

    PushConstantsData push_constants {};
    Stats stats {};
    LiteMath::float4 clear_color {0.25f, 0.25f, 0.25f, 1.0f};

    uint32_t frame_index {0};

private:
    VkPipeline compute_hz_buffer_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_hz_buffer_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline compute_prepare_indirect_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout compute_prepare_indirect_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline forward_rendering_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout forward_rendering_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline frustum_demo_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout frustum_demo_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline graphics_gbuffer_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout graphics_gbuffer_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline graphics_lighting_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout graphics_lighting_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline marching_cubes_octree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout marching_cubes_octree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline marching_cubes_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout marching_cubes_scomtree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline mesh_shading_octree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout mesh_shading_octree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline mesh_shading_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout mesh_shading_scomtree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline mesh_gbuffer_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout mesh_gbuffer_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline traverse_octree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout traverse_octree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline traverse_scomtree_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout traverse_scomtree_pipeline_layout {VK_NULL_HANDLE};

    VkPipeline mesh_shading_scomtree_deferred_pipeline {VK_NULL_HANDLE};
    VkPipelineLayout mesh_shading_scomtree_deferred_pipeline_layout {VK_NULL_HANDLE};
};

} // namespace sdf_raster
