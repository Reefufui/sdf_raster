// engine/renderer.cpp
#include "renderer.hpp"

#include "logger.hpp"
#include "scenes/obj/obj.hpp"
#include "scenes/octree/octree.hpp"
#include "scenes/scomtree/scomtree.hpp"
#include "application/gui/presentation_render_target.hpp"

#include <spdlog/stopwatch.h>
#include <vk_buffers.h>
#include <vk_pipeline.h>

#include <array>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace sdf_raster {

#define RENDERER_NAME "Renderer"

Renderer::Renderer (std::shared_ptr <VulkanContext> context, std::shared_ptr <RenderTarget> render_target)
    : context (context)
    , render_target (render_target) {
    if (!this->context) {
        throw std::invalid_argument("VulkanContext cannot be null.");
    }

    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error ("VulkanContext is not initialized before renderer init.");
    }

    this->init_push_constants (); // TODO: init in set_scene ?

    this->frustum_ds = std::make_unique <FrustumDescriptorSetInfo> (this->context->get_device ()
        , this->context->get_physical_device ()
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->render_target->get_max_frames_in_flight ());

    this->forward_shading = std::make_unique <ForwardShading> (this->context->get_device ()
        , this->context->get_physical_device ()
        , this->render_target);

    this->dummy_ds = std::make_unique <DummyDescriptorSetInfo> (this->context->get_device ()
        , this->context->get_physical_device ()
        , this->context->get_transfer_command_pool_reset ()
        , this->context->get_transfer_queue ()
        , VK_SHADER_STAGE_ALL
        , this->render_target->get_extent ()
        , this->render_target->get_max_frames_in_flight ());
}

Renderer::~Renderer () {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->context || !this->context->is_initialized ()) {
        LOG_ERROR ("Vulkan context is already missing");
        return;
    }

    if (this->frustum_draw_buffer) {
        if (this->current_model) {
            this->current_model->get_state ().camera = this->frustum_draw_buffer->get_camera ();
        }
        this->frustum_draw_buffer.reset ();
    }

    this->release_render_resources ();

    this->dummy_ds.reset ();
    this->frustum_ds.reset ();
}

void Renderer::destroy_pipelines () {
    if (!this->context) {
        return;
    }

    vkDeviceWaitIdle (this->context->get_device ());

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_hz_buffer_pipeline, this->compute_hz_buffer_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prepare_indirect_pipeline, this->compute_prepare_indirect_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->forward_rendering_pipeline, this->forward_rendering_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->frustum_demo_pipeline, this->frustum_demo_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_gbuffer_pipeline, this->graphics_gbuffer_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_lighting_pipeline, this->graphics_lighting_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->marching_cubes_octree_pipeline, this->marching_cubes_octree_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->marching_cubes_scomtree_pipeline, this->marching_cubes_scomtree_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->mesh_shading_octree_pipeline, this->mesh_shading_octree_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->mesh_shading_scomtree_pipeline, this->mesh_shading_scomtree_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->mesh_gbuffer_pipeline, this->mesh_gbuffer_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->traverse_octree_pipeline, this->traverse_octree_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->traverse_scomtree_pipeline, this->traverse_scomtree_pipeline_layout);
}

void Renderer::create_required_pipelines () {
    if (!this->current_model) {
        return;
    }

    const auto method = this->current_model->get_state ().draw_method;

    if (method == DrawMethod::OctreeCompute
        || method == DrawMethod::OctreeMesh
        || method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeComputeDeferred
        || method == DrawMethod::SComTreeMesh || method == DrawMethod::SComTreeMeshDeferred
       ) {
        this->init_compute_hz_buffer_pipeline ();
        this->init_compute_prepare_indirect_pipeline ();
    }

    if (method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        this->init_forward_rendering_pipeline ("shaders/view_proj.vert.slang.spv", "shaders/blinn_phong.frag.slang.spv", VK_FRONT_FACE_CLOCKWISE);
    }

    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeMesh) {
        this->init_forward_rendering_pipeline ("shaders/view_proj.vert.slang.spv", "shaders/blinn_phong.frag.slang.spv", VK_FRONT_FACE_COUNTER_CLOCKWISE);
    }

    if (method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        this->init_traverse_octree_pipeline ();
        this->init_marching_cubes_octree_pipeline ();
    }

    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeMesh
        || method == DrawMethod::SComTreeComputeDeferred || method == DrawMethod::SComTreeMeshDeferred) {
        this->init_traverse_scomtree_pipeline ();
        this->init_marching_cubes_scomtree_pipeline ();
    }

    if (method == DrawMethod::SComTreeComputeDeferred) {
        this->init_graphics_gbuffer_pipeline ("shaders/view_proj.vert.slang.spv", "shaders/gbuffer.frag.slang.spv");
        this->init_graphics_lighting_pipeline ();
    }

    if (method == DrawMethod::SComTreeMeshDeferred) {
        this->init_mesh_gbuffer_pipeline ("shaders/marching_cubes_scomtree.mesh.slang.spv", "shaders/gbuffer.frag.slang.spv");
        this->init_graphics_lighting_pipeline ();
    }

    if (method == DrawMethod::Explicit) {
        this->init_forward_rendering_pipeline ("shaders/view_proj.vert.slang.spv", "shaders/blinn_phong.frag.slang.spv", VK_FRONT_FACE_CLOCKWISE);
    }

    if (method == DrawMethod::ExplicitDeferred) {
        this->init_graphics_gbuffer_pipeline ("shaders/view_proj.vert.slang.spv", "shaders/gbuffer.frag.slang.spv");
        this->init_graphics_lighting_pipeline ();
    }

    if (method == DrawMethod::SComTreeMesh) {
        this->init_mesh_shading_scomtree_pipeline ();
    }

    if (method == DrawMethod::OctreeMesh) {
        this->init_mesh_shading_octree_pipeline ();
    }

    this->init_frustum_demo_pipeline ();
}

void Renderer::resize () {
        this->destroy_pipelines ();

        if (this->hz_buffer_ds) {
            this->hz_buffer_ds.reset ();
            this->hz_buffer_ds = std::make_unique <HZBufferDescriptorSetInfo> (this->context->get_device ()
                , this->context->get_physical_device ()
                , this->context->get_transfer_command_pool_reset ()
                , this->context->get_transfer_queue ()
                , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                , this->render_target->get_extent ()
                , this->render_target->get_max_frames_in_flight ());
        }

        if (this->forward_shading) {
            this->forward_shading.reset ();
            this->forward_shading = std::make_unique <ForwardShading> (this->context->get_device ()
                , this->context->get_physical_device ()
                , this->render_target);
        } else if (this->deferred_shading) {
            this->deferred_shading.reset ();
            this->deferred_shading = std::make_unique <DeferredShading> (this->context->get_device ()
                , this->context->get_physical_device ()
                , this->context->get_transfer_command_pool_reset ()
                , this->context->get_transfer_queue ()
                , DeferredShadingConfig {
                    .gbuffer_formats = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM },
                    .filter = VK_FILTER_LINEAR
                }
                , this->render_target);
        }

        this->create_required_pipelines ();

}

void Renderer::init_push_constants () {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties (this->context->get_physical_device (), &device_properties);
    const uint32_t max_push_constant_size = device_properties.limits.maxPushConstantsSize;
    const uint32_t required_push_constant_size = sizeof (PushConstantsData);
    if (required_push_constant_size > max_push_constant_size) {
        LOG_CRITICAL ("[{}] Required push constants size ({}) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize ({})."
            , RENDERER_NAME, required_push_constant_size, max_push_constant_size);
        throw std::runtime_error ("sizeof (PushConstantsData) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize");
    }

    this->push_constants.active_leafs_max_count = 1500000; // TODO: settings
}

void Renderer::init_compute_hz_buffer_pipeline () {
    assert (this->context && "required for 'traverse_octree_pipeline'");
    assert (this->hz_buffer_ds && "required for 'compute_hz_buffer_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/hz_buffer.comp.slang.spv");
    this->compute_hz_buffer_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->hz_buffer_ds->get_gen_layout ()
        }, 0);
    this->compute_hz_buffer_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_traverse_octree_pipeline () {
    assert (this->context && "required for 'traverse_octree_pipeline'");
    assert (this->sdf_octree_ds && "required for 'traverse_octree_pipeline'");
    assert (this->active_leafs_ds && "required for 'traverse_octree_pipeline'");
    assert (this->frustum_ds && "required for 'traverse_octree_pipeline'");
    assert (this->hz_buffer_ds && "required for 'traverse_octree_pipeline'");
    assert (this->lod_ds && "required for 'traverse_octree_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/octree_traversal.comp.slang.spv");
    this->traverse_octree_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->sdf_octree_ds->get_layout ()
        , this->active_leafs_ds->get_layout ()
        , this->frustum_ds->get_layout ()
        , this->hz_buffer_ds->get_layout ()
        , this->lod_ds->get_layout ()}, sizeof (PushConstantsData));
    this->traverse_octree_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_traverse_scomtree_pipeline () {
    assert (this->context && "required for 'traverse_scomtree_pipeline'");
    assert (this->sdf_scomtree_ds && "required for 'traverse_scomtree_pipeline'");
    assert (this->active_leafs_ds && "required for 'traverse_scomtree_pipeline'");
    assert (this->frustum_ds && "required for 'traverse_scomtree_pipeline'");
    assert (this->hz_buffer_ds && "required for 'traverse_scomtree_pipeline'");
    assert (this->lod_ds && "required for 'traverse_scomtree_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/scomtree_traversal.comp.slang.spv");
    this->traverse_scomtree_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->sdf_scomtree_ds->get_layout ()
        , this->active_leafs_ds->get_layout ()
        , this->frustum_ds->get_layout ()
        , this->hz_buffer_ds->get_layout ()
        , this->lod_ds->get_layout ()}, sizeof (PushConstantsData));
    this->traverse_scomtree_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_compute_prepare_indirect_pipeline () {
    assert (this->context && "required for 'compute_prepare_indirect_pipeline'");
    assert (this->active_leafs_ds && "required for 'compute_prepare_indirect_pipeline'");
    assert (this->indirect_dispatch_ds && "required for 'compute_prepare_indirect_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/linear_indirect_prep.comp.slang.spv");
    this->compute_prepare_indirect_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds->get_layout ()
            , this->indirect_dispatch_ds->get_layout ()
        }, sizeof (uint32_t));
    this->compute_prepare_indirect_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_marching_cubes_octree_pipeline () {
    assert (this->context && "required for 'marching_cubes_octree_pipeline'");
    assert (this->sdf_octree_ds && "required for 'marching_cubes_octree_pipeline'");
    assert (this->mesh_ds && "required for 'marching_cubes_octree_pipeline'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'marching_cubes_octree_pipeline'");
    assert (this->active_leafs_ds && "required for 'marching_cubes_octree_pipeline'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'marching_cubes_octree_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/marching_cubes_octree.comp.slang.spv");
    this->marching_cubes_octree_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds->get_layout ()
            , this->mesh_ds->get_layout ()
            , this->marching_cubes_lookup_table_ds->get_layout ()
            , this->active_leafs_ds->get_layout ()
            , this->draw_indexed_indirect_command_ds->get_layout ()
        }, sizeof (PushConstantsData));
    this->marching_cubes_octree_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_marching_cubes_scomtree_pipeline () {
    assert (this->context && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->sdf_scomtree_ds && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->mesh_ds && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->active_leafs_ds && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'marching_cubes_scomtree_pipeline'");
    assert (this->lod_ds && "required for 'marching_cubes_scomtree_pipeline'");

    vk_utils::ComputePipelineMaker maker;
    maker.LoadShader (this->context->get_device (), "shaders/marching_cubes_scomtree.comp.slang.spv");
    this->marching_cubes_scomtree_pipeline_layout = maker.MakeLayout (this->context->get_device ()
        , {
            this->sdf_scomtree_ds->get_layout ()
            , this->mesh_ds->get_layout ()
            , this->marching_cubes_lookup_table_ds->get_layout ()
            , this->active_leafs_ds->get_layout ()
            , this->draw_indexed_indirect_command_ds->get_layout ()
            , this->lod_ds->get_layout ()
          }
        , sizeof (PushConstantsData));
    this->marching_cubes_scomtree_pipeline = maker.MakePipeline (this->context->get_device ());
}

void Renderer::init_forward_rendering_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path, VkFrontFace front_face) {
    assert (this->context && "required for 'init_forward_rendering_pipeline'");
    assert (this->forward_shading && "required for 'init_forward_rendering_pipeline'");

    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_VERTEX_BIT, vert_shader_path},
        {VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader_path}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height);
    maker.rasterizer.frontFace = front_face;

    this->forward_rendering_pipeline_layout = maker.MakeLayout (this->context->get_device (), {}, sizeof (PushConstantsData));

    VkVertexInputBindingDescription binding_desc {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof (Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector <VkVertexInputAttributeDescription> attribute_descriptions (3);
    attribute_descriptions [0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}; // pos
    attribute_descriptions [1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof (float) * 4}; // norm
    attribute_descriptions [2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof (float) * 8}; // color

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_layout.vertexBindingDescriptionCount = 1;
    vertex_layout.pVertexBindingDescriptions = &binding_desc;
    vertex_layout.vertexAttributeDescriptionCount = static_cast <uint32_t> (attribute_descriptions.size ());
    vertex_layout.pVertexAttributeDescriptions = attribute_descriptions.data ();

    this->forward_rendering_pipeline = maker.MakePipeline (this->context->get_device (), vertex_layout, this->forward_shading->get_render_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_graphics_gbuffer_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path) {
    assert (this->context && "required for 'graphics_gbuffer_pipeline'");
    assert (this->deferred_shading && "required for 'graphics_gbuffer_pipeline'");

    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_VERTEX_BIT, vert_shader_path},
        {VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader_path}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height, 3);
    maker.rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    this->graphics_gbuffer_pipeline_layout = maker.MakeLayout (this->context->get_device (), {}, sizeof (PushConstantsData));

    VkVertexInputBindingDescription binding_desc {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof (Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector <VkVertexInputAttributeDescription> attribute_descriptions (3);
    attribute_descriptions [0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0}; // pos
    attribute_descriptions [1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof (float) * 4}; // norm
    attribute_descriptions [2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof (float) * 8}; // color

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_layout.vertexBindingDescriptionCount = 1;
    vertex_layout.pVertexBindingDescriptions = &binding_desc;
    vertex_layout.vertexAttributeDescriptionCount = static_cast <uint32_t> (attribute_descriptions.size ());
    vertex_layout.pVertexAttributeDescriptions = attribute_descriptions.data ();

    this->graphics_gbuffer_pipeline = maker.MakePipeline (this->context->get_device (), vertex_layout, this->deferred_shading->get_gbuffer_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_mesh_gbuffer_pipeline (const std::string& vert_shader_path, const std::string& frag_shader_path) {
    assert (this->context && "required for 'mesh_gbuffer_pipeline'");
    assert (this->active_leafs_ds && "required for 'mesh_gbuffer_pipeline'");
    assert (this->deferred_shading && "required for 'mesh_gbuffer_pipeline'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'mesh_gbuffer_pipeline'");
    assert (this->sdf_scomtree_ds && "required for 'mesh_gbuffer_pipeline'");

    vk_utils::MeshPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_MESH_BIT_EXT, vert_shader_path},
        {VK_SHADER_STAGE_FRAGMENT_BIT, frag_shader_path}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height, 3);

    this->mesh_gbuffer_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->sdf_scomtree_ds->get_layout (),
        this->marching_cubes_lookup_table_ds->get_layout (),
        this->active_leafs_ds->get_layout (),
        this->lod_ds->get_layout ()
    }, sizeof (PushConstantsData));

    this->mesh_gbuffer_pipeline = maker.MakePipeline (this->context->get_device (), this->deferred_shading->get_gbuffer_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_graphics_lighting_pipeline () {
    assert (this->context && "required for 'graphics_lighting_pipeline'");
    assert (this->deferred_shading && "required for 'graphics_lighting_pipeline'");

    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/fullscreen_quad.vert.slang.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/deferred_lighting.frag.slang.spv"}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height);
    maker.depthStencilTest.depthTestEnable = VK_FALSE;
    maker.depthStencilTest.depthWriteEnable = VK_FALSE;

    std::vector <VkDescriptorSetLayout> layouts {};
    layouts.push_back (this->deferred_shading->get_layout ());
    if (this->hz_buffer_ds) {
        layouts.push_back (this->hz_buffer_ds->get_base_level_layout ());
    } else {
        layouts.push_back (this->dummy_ds->get_storage_image_ds_layout ());
    }

    this->graphics_lighting_pipeline_layout = maker.MakeLayout (this->context->get_device (), layouts, sizeof (DeferredLightingPushConstants));

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    this->graphics_lighting_pipeline = maker.MakePipeline (this->context->get_device (), vertex_layout, this->deferred_shading->get_lighting_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_mesh_shading_octree_pipeline () {
    assert (this->context && "required for 'mesh_shading_octree_pipeline'");
    assert (this->active_leafs_ds && "required for 'mesh_shading_octree_pipeline'");
    assert (this->forward_shading && "required for 'mesh_shading_octree_pipeline'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'mesh_shading_octree_pipeline'");
    assert (this->sdf_octree_ds && "required for 'mesh_shading_octree_pipeline'");

    vk_utils::MeshPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_MESH_BIT_EXT, "./shaders/marching_cubes_octree.mesh.slang.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/blinn_phong.frag.slang.spv"}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height);
    maker.rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    this->mesh_shading_octree_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->sdf_octree_ds->get_layout (),
        this->marching_cubes_lookup_table_ds->get_layout (),
        this->active_leafs_ds->get_layout (),
        this->lod_ds->get_layout ()
    }, sizeof (PushConstantsData));

    this->mesh_shading_octree_pipeline = maker.MakePipeline (this->context->get_device (), this->forward_shading->get_render_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_mesh_shading_scomtree_pipeline () {
    assert (this->context && "required for 'mesh_shading_scomtree_pipeline'");
    assert (this->active_leafs_ds && "required for 'mesh_shading_scomtree_pipeline'");
    assert (this->forward_shading && "required for 'mesh_shading_scomtree_pipeline'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'mesh_shading_scomtree_pipeline'");
    assert (this->sdf_scomtree_ds && "required for 'mesh_shading_scomtree_pipeline'");

    vk_utils::MeshPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_MESH_BIT_EXT, "./shaders/marching_cubes_scomtree.mesh.slang.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/blinn_phong.frag.slang.spv"}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height);
    maker.rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    this->mesh_shading_scomtree_pipeline_layout = maker.MakeLayout (this->context->get_device (), {
        this->sdf_scomtree_ds->get_layout (),
        this->marching_cubes_lookup_table_ds->get_layout (),
        this->active_leafs_ds->get_layout (),
        this->lod_ds->get_layout ()
    }, sizeof (PushConstantsData));

    this->mesh_shading_scomtree_pipeline = maker.MakePipeline (this->context->get_device (), this->forward_shading->get_render_pass (), {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });
}

void Renderer::init_frustum_demo_pipeline () {
    assert (this->context && "required for 'frustum_demo_pipeline'");
    assert (this->frustum_ds && "required for 'frustum_demo_pipeline'");
    assert ((this->forward_shading || this->deferred_shading) && "required for 'init_frustum_demo_pipeline");

    vk_utils::GraphicsPipelineMaker maker;
    maker.LoadShaders (this->context->get_device (), {
        {VK_SHADER_STAGE_VERTEX_BIT, "shaders/frustum_view.vert.slang.spv"},
        {VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/frustum_view.frag.slang.spv"}
    });

    auto extent = this->render_target->get_extent ();
    maker.SetDefaultState (extent.width, extent.height);
    maker.inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    maker.depthStencilTest.depthWriteEnable = VK_FALSE;

    this->frustum_demo_pipeline_layout = maker.MakeLayout (this->context->get_device (), {}, sizeof (PushConstantsData));

    VkVertexInputBindingDescription binding_desc {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof (LiteMath::float4);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute_description {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_layout.vertexBindingDescriptionCount = 1;
    vertex_layout.pVertexBindingDescriptions = &binding_desc;
    vertex_layout.vertexAttributeDescriptionCount = 1;
    vertex_layout.pVertexAttributeDescriptions = &attribute_description;

    VkRenderPass render_pass = this->forward_shading ? this->forward_shading->get_render_pass_after () : this->deferred_shading->get_render_pass_after ();

    this->frustum_demo_pipeline = maker.MakePipeline (this->context->get_device (), vertex_layout, render_pass, {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    }, maker.inputAssembly);
}

void Renderer::set_default_viewport_and_scissor (VkCommandBuffer cmd_buff) {
    auto extent = this->render_target->get_extent ();
    VkViewport viewport {
        .x = 0.0f, .y = 0.0f,
        .width = static_cast <float> (extent.width), .height = static_cast <float> (extent.height),
        .minDepth = 0.0f, .maxDepth = 1.0f
    };
    VkRect2D scissor {{0, 0}, extent};
    vkCmdSetViewport (cmd_buff, 0, 1, &viewport);
    vkCmdSetScissor (cmd_buff, 0, 1, &scissor);
}

void Renderer::update (uint32_t frame_index, Settings& settings, float delta_time) {
    assert (frame_index < this->render_target->get_max_frames_in_flight () && "frame_index must be less than max_frames_in_flight");

    this->frame_index = frame_index;

    if (!this->current_model) {
        return;
    }

    auto& model_state = current_model->get_state ();
    model_state.camera.update (delta_time);

    if (settings.animate_rotation) {
        model_state.rotation.y += 45.0f * delta_time;
        if (model_state.rotation.y > 360.0f) model_state.rotation.y -= 360.0f;
    }

    if (!settings.frustum_view && this->frustum_draw_buffer) {
        this->clear_color = this->clear_color * 2.f;
        model_state.camera = this->frustum_draw_buffer->get_camera ();
        this->frustum_draw_buffer.reset ();
        LOG_INFO ("[{}] Frustum view mode: OFF.", RENDERER_NAME);
    } else if (settings.frustum_view && !this->frustum_draw_buffer) {
        this->clear_color = this->clear_color * 0.5f;
        this->frozen_camera_pos = LiteMath::to_float4 (model_state.camera.get_position (), 1.0f);
        this->frustum_draw_buffer = FrustumDrawBuffer::get_frustum_buffer (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , model_state.camera);
        LOG_INFO ("[{}] Frustum view mode: ON.", RENDERER_NAME);
    }

    this->stats.active_leafs_count = (this->active_leafs_ds) ? this->active_leafs_ds->fetch_active_leaf_counter (this->frame_index) : 0;
    if (this->sdf_octree_ds) {
        this->stats.active_roots_count = this->sdf_octree_ds->get_subtree_count ();
    } else if (this->sdf_scomtree_ds) {
    this->stats.active_roots_count = this->sdf_scomtree_ds->get_subtree_count ();
    } else {
        this->stats.active_roots_count = 0;
    }

    if (this->lod_ds) {
        const auto lod = this->lod_ds->fetch_lod (this->frame_index, 0);
        this->stats.lod = lod.lod;
        this->stats.distance = lod.distance;
    } else {
        this->stats.lod = 0;
        this->stats.distance = 0;
    }

    if (this->hz_buffer_ds) {
        this->push_constants.prev_mvp = this->hz_buffer_ds->frame_resources_ref (this->frame_index).prev_mvp;
    }

    this->push_constants.view_proj = model_state.camera.get_view_projection_matrix ();
    if (settings.frustum_view && this->frustum_draw_buffer) {
        this->push_constants.camera_pos = this->frozen_camera_pos;
    } else {
        this->push_constants.camera_pos = LiteMath::to_float4 (model_state.camera.get_position (), 1.0f);
    }
    this->push_constants.far_plane = model_state.camera.get_far_plane ();
    this->push_constants.near_plane = model_state.camera.get_near_plane ();
    this->push_constants.max_lod = static_cast <uint> (model_state.max_lod);
    this->push_constants.min_lod = static_cast <uint> (model_state.min_lod);
    this->push_constants.subtree_root_level = static_cast <uint> (model_state.cpu_traversed);
    this->push_constants.occlusion_culling_level = static_cast <uint> (model_state.occlusion_culling_level);
    this->push_constants.frustum_culling_level = static_cast <uint> (model_state.frustum_culling_level);
    this->push_constants.color_leafs = settings.color_leafs;
    this->push_constants.lod_mode = static_cast <uint> (model_state.lod_mode);
    this->push_constants.fixed_lod = static_cast <uint> (model_state.fixed_lod);
    this->push_constants.root_center = model_state.octree_root_center;
    this->push_constants.lod_threshold_pixels = model_state.lod_threshold_pixels;
    this->push_constants.lod_aggressivity = model_state.lod_aggressivity;
    this->push_constants.fov_y = model_state.camera.get_fov_y () * (3.14159265359f / 180.0f);
    auto extent = this->render_target->get_extent ();
    this->push_constants.screen_width = extent.width;
    this->push_constants.screen_height = extent.height;
    this->push_constants.max_voxel_size = 2.0f / std::pow (2.0f, model_state.cpu_traversed);
    this->push_constants.min_voxel_size = 2.0f / std::pow (2.0f, model_state.octree_depth);
    this->clear_color = settings.lighting.clear_color;

    if (this->deferred_shading) {
        auto& lighting_pc = this->deferred_shading->push_constants_ref ();
        const auto& lighting = settings.lighting;

        LiteMath::float4x4 inv_model;
        if (this->current_model) {
            inv_model = LiteMath::inverse4x4 (this->current_model->get_model_matrix ());
        }

        lighting_pc.camera_pos        = inv_model * LiteMath::to_float4 (model_state.camera.get_position (), 1.0f);
        lighting_pc.light_pos         = inv_model * LiteMath::to_float4 (lighting.light_pos, 1.0f);
        lighting_pc.light_color       = LiteMath::to_float4 (lighting.light_color, 1.0f);
        lighting_pc.fog_color         = LiteMath::to_float4 (lighting.fog_color, 1.0f);

        lighting_pc.ambient_strength  = lighting.ambient_strength;
        lighting_pc.specular_strength = lighting.specular_strength;
        lighting_pc.shininess         = lighting.shininess;
        lighting_pc.depth_threshold   = lighting.depth_threshold;

        lighting_pc.fog_start         = lighting.fog_start;
        lighting_pc.fog_end           = lighting.fog_end;

        lighting_pc.enable_hz_write   = !!this->hz_buffer_ds && !this->frustum_draw_buffer;
    }

    if (!settings.frustum_view && this->frustum_ds) {
        FrustumGeometry* ptr = static_cast <FrustumGeometry*> (this->frustum_ds->get_frustum_geometry_memory_ptr (this->frame_index));
        
        LiteMath::float4x4 inv_model;
        if (this->current_model) {
            inv_model = LiteMath::inverse4x4 (this->current_model->get_model_matrix ());
        }

        this->update_frustum_buffer (model_state.camera, inv_model);
        *ptr = this->frustum;

        if (this->sdf_octree_ds) {
            this->sdf_octree_ds->update_subtree_root_buffer (this->frustum, this->frame_index);
        } else if (this->sdf_scomtree_ds) {
            this->sdf_scomtree_ds->update_subtree_root_buffer (this->frustum, this->frame_index);
        }
    }
}

namespace {

LiteMath::float3 face_normal (const LiteMath::float4& a, const LiteMath::float4& b, const LiteMath::float4& c) {
    LiteMath::float3 ab = LiteMath::to_float3 (b - a);
    LiteMath::float3 ac = LiteMath::to_float3 (c - a);
    return LiteMath::normalize (LiteMath::cross (ab, ac));
}

}

void Renderer::update_frustum_buffer (const Camera& camera, const LiteMath::float4x4& inv_model) {
    const auto& vertices = camera.get_frustum_corners ();
    
    for (size_t i = 0; i < 8; ++i) {
        this->frustum.vertices [i] = inv_model * vertices [i];
    }

    this->frustum.normals [0] = LiteMath::to_float4 (face_normal (this->frustum.vertices [1], this->frustum.vertices [0], this->frustum.vertices [2]), 1.f); // Near
    this->frustum.normals [1] = LiteMath::to_float4 (face_normal (this->frustum.vertices [4], this->frustum.vertices [5], this->frustum.vertices [7]), 1.f); // Far
    this->frustum.normals [2] = LiteMath::to_float4 (face_normal (this->frustum.vertices [0], this->frustum.vertices [4], this->frustum.vertices [6]), 1.f); // Left
    this->frustum.normals [3] = LiteMath::to_float4 (face_normal (this->frustum.vertices [5], this->frustum.vertices [1], this->frustum.vertices [3]), 1.f); // Right
    this->frustum.normals [4] = LiteMath::to_float4 (face_normal (this->frustum.vertices [2], this->frustum.vertices [3], this->frustum.vertices [7]), 1.f); // Top
    this->frustum.normals [5] = LiteMath::to_float4 (face_normal (this->frustum.vertices [4], this->frustum.vertices [0], this->frustum.vertices [1]), 1.f); // Bottom
}

void Renderer::clear_geometry (VkCommandBuffer cmd_buff) {
    vkCmdFillBuffer (cmd_buff, this->mesh_ds->get_vertex_buffer (this->frame_index), 0, VK_WHOLE_SIZE, 0x00000000);
    vkCmdFillBuffer (cmd_buff, this->mesh_ds->get_index_buffer (this->frame_index), 0, VK_WHOLE_SIZE, 0x00000000);

    VkBufferMemoryBarrier buffer_barrier = {};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.pNext = nullptr;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.offset = 0;
    buffer_barrier.size = VK_WHOLE_SIZE;

    std::vector <VkBufferMemoryBarrier> barriers (2, buffer_barrier);
    barriers [0].buffer = this->mesh_ds->get_vertex_buffer (this->frame_index);
    barriers [1].buffer = this->mesh_ds->get_index_buffer (this->frame_index);

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        static_cast <uint32_t> (barriers.size ()), barriers.data (),
        0, nullptr
    );
}

void Renderer::copy_forward_rendered_depth (VkCommandBuffer cmd_buff) {
    assert (this->forward_shading && "required for 'copy_forward_rendered_depth'");
    assert (this->hz_buffer_ds && "required for 'copy_forward_rendered_depth'");

    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds->frame_resources_ref (this->frame_index);

    if (!this->frustum_draw_buffer && this->push_constants.occlusion_culling_level) {
        VkImageMemoryBarrier depth_to_src = {};
        depth_to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth_to_src.pNext = nullptr;
        depth_to_src.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        depth_to_src.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        depth_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_src.image = this->forward_shading->get_depth_image ();
        depth_to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_to_src.subresourceRange.baseMipLevel = 0;
        depth_to_src.subresourceRange.levelCount = 1;
        depth_to_src.subresourceRange.baseArrayLayer = 0;
        depth_to_src.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier (cmd_buff
            , VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
            , VK_PIPELINE_STAGE_TRANSFER_BIT
            , 0
            , 0, nullptr
            , 0, nullptr
            , 1, &depth_to_src);

        VkBufferImageCopy buffer_image_copy_region {
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { this->hz_buffer_ds->get_extent ().width, this->hz_buffer_ds->get_extent ().height, 1 }
        };

        vkCmdCopyImageToBuffer (cmd_buff
            , this->forward_shading->get_depth_image ()
            , VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
            , f.transition_buffer
            , 1, &buffer_image_copy_region);

        VkBufferMemoryBarrier transition_barrier {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = f.transition_buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        vkCmdPipelineBarrier (cmd_buff
            , VK_PIPELINE_STAGE_TRANSFER_BIT
            , VK_PIPELINE_STAGE_TRANSFER_BIT
            , 0
            , 0, nullptr
            , 1, &transition_barrier
            , 0, nullptr);

        buffer_image_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        vkCmdCopyBufferToImage (cmd_buff
            , f.transition_buffer
            , f.hz_buffer.image
            , VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            , 1, &buffer_image_copy_region);

        VkImageMemoryBarrier depth_to_attachment = {};
        depth_to_attachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth_to_attachment.pNext = nullptr;
        depth_to_attachment.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        depth_to_attachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth_to_attachment.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        depth_to_attachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_to_attachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_attachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depth_to_attachment.image = this->forward_shading->get_depth_image ();
        depth_to_attachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_to_attachment.subresourceRange.baseMipLevel = 0;
        depth_to_attachment.subresourceRange.levelCount = 1;
        depth_to_attachment.subresourceRange.baseArrayLayer = 0;
        depth_to_attachment.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier (cmd_buff
            , VK_PIPELINE_STAGE_TRANSFER_BIT
            , VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
            , 0
            , 0, nullptr
            , 0, nullptr
            , 1, &depth_to_attachment);
    }
}

void Renderer::copy_subtrees (VkCommandBuffer cmd_buff) {
    VkDeviceSize subtrees_size {};

    if (this->sdf_octree_ds) {
        subtrees_size = this->sdf_octree_ds->get_subtree_count () * sizeof (NodeContext);
    } else {
        subtrees_size = this->sdf_scomtree_ds->get_subtree_count () * sizeof (SComTreeStackElement);
    }

    if (!subtrees_size) {
        return;
    }

    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = subtrees_size;

    if (this->sdf_octree_ds) {
        vkCmdCopyBuffer (cmd_buff, this->sdf_octree_ds->get_subtree_root_staging_buffer (this->frame_index), this->sdf_octree_ds->get_subtree_root_buffer (this->frame_index), 1, &copy_region);
    } else if (this->sdf_scomtree_ds) {
        vkCmdCopyBuffer (cmd_buff, this->sdf_scomtree_ds->get_subtree_root_staging_buffer (this->frame_index), this->sdf_scomtree_ds->get_subtree_root_buffer (this->frame_index), 1, &copy_region);
    } else {
        assert (false && "missing source of subtrees");
    }

    VkBufferMemoryBarrier barr = {};
    barr.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barr.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; 
    barr.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    if (this->sdf_octree_ds) {
        barr.buffer = this->sdf_octree_ds->get_subtree_root_buffer (this->frame_index);
    } else if (this->sdf_scomtree_ds) {
        barr.buffer = this->sdf_scomtree_ds->get_subtree_root_buffer (this->frame_index);
    }
    barr.offset = 0;
    barr.size = subtrees_size;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_TRANSFER_BIT
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , 0
        , 0, nullptr
        , 1, &barr
        , 0, nullptr
    );
}

void Renderer::compute_hz_buffer (VkCommandBuffer cmd_buff) {
    // NOTE: expects layout of all hz_buffer_ds mip-images to be VK_IMAGE_LAYOUT_GENERAL

    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds->frame_resources_ref (this->frame_index);

    if (this->push_constants.occlusion_culling_level) {
        vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_hz_buffer_pipeline);

        for (uint32_t i = 0; i < f.hz_buffer.mipLvls - 1; ++i) {
            const uint32_t dstMip = i + 1;

            uint32_t dstWidth = std::max (1u, this->hz_buffer_ds->get_extent ().width >> dstMip);
            uint32_t dstHeight = std::max (1u, this->hz_buffer_ds->get_extent ().height >> dstMip);

            vkCmdBindDescriptorSets (cmd_buff
                , VK_PIPELINE_BIND_POINT_COMPUTE
                , this->compute_hz_buffer_pipeline_layout
                , 0, 1, &f.gen_descriptor_sets [i]
                , 0, nullptr);

            uint32_t groupX = (dstWidth + 15) / 16;
            uint32_t groupY = (dstHeight + 15) / 16;
            vkCmdDispatch (cmd_buff, groupX, groupY, 1);

            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = f.hz_buffer.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = dstMip;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier (cmd_buff
                , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                , 0
                , 0, nullptr
                , 0, nullptr
                , 1, &barrier);
        }
    }
}

void Renderer::reset_active_leafs_counter (VkCommandBuffer cmd_buff) {
    vkCmdFillBuffer (cmd_buff, this->active_leafs_ds->get_active_leaf_counter_buffer (this->frame_index), 0, VK_WHOLE_SIZE, 0x00000000);

    VkBufferMemoryBarrier buffer_barrier = {};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.pNext = nullptr;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = this->active_leafs_ds->get_active_leaf_counter_buffer (this->frame_index);
    buffer_barrier.offset = 0;
    buffer_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &buffer_barrier,
        0, nullptr
    );
}

void Renderer::hz_buffer_barrier (VkCommandBuffer cmd_buff, LayoutStageAccess src, LayoutStageAccess dst) {
    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds->frame_resources_ref (this->frame_index);

    VkImageSubresourceRange base_level_range {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkImageMemoryBarrier base_level_barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src.access,
        .dstAccessMask = dst.access,
        .oldLayout = src.layout,
        .newLayout = dst.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = f.hz_buffer.image,
        .subresourceRange = base_level_range
    };

    vkCmdPipelineBarrier (cmd_buff, src.stage, dst.stage, 0, 0, nullptr, 0, nullptr, 1, &base_level_barrier);
}

void Renderer::hz_buffer_barrier (VkCommandBuffer cmd_buff, LayoutStageAccess src_base, LayoutStageAccess dst_base, LayoutStageAccess src_levels, LayoutStageAccess dst_levels) {
    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds->frame_resources_ref (this->frame_index);

    VkImageSubresourceRange base_level_range {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkImageSubresourceRange levels_range {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 1,
        .levelCount = f.hz_buffer.mipLvls - 1,
        .baseArrayLayer = 0,
        .layerCount = 1
    };

    VkImageMemoryBarrier base_level_barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_base.access,
        .dstAccessMask = dst_base.access,
        .oldLayout = src_base.layout,
        .newLayout = dst_base.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = f.hz_buffer.image,
        .subresourceRange = base_level_range
    };

    VkImageMemoryBarrier levels_barrier {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = src_levels.access,
        .dstAccessMask = dst_levels.access,
        .oldLayout = src_levels.layout,
        .newLayout = dst_levels.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = f.hz_buffer.image,
        .subresourceRange = levels_range
    };

    std::array <VkImageMemoryBarrier, 2> barriers {base_level_barrier, levels_barrier};

    vkCmdPipelineBarrier (cmd_buff
        , src_base.stage | src_levels.stage
        , dst_base.stage | dst_levels.stage
        , 0, 0, nullptr, 0, nullptr
        , static_cast <uint32_t> (barriers.size ()), barriers.data ());
}

void Renderer::traverse_octree (VkCommandBuffer cmd_buff) {
    size_t subtree_root_count = this->sdf_octree_ds->get_subtree_count ();
    if (!subtree_root_count) {
        return;
    }

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->traverse_octree_pipeline);

    std::array <VkDescriptorSet, 5> ds {};
    ds [0] = this->sdf_octree_ds->get_descriptor_set (this->frame_index);
    ds [1] = this->active_leafs_ds->get_descriptor_set (this->frame_index);
    ds [2] = this->frustum_ds->get_descriptor_set (this->frame_index);
    ds [3] = this->hz_buffer_ds->frame_resources_ref (this->frame_index).descriptor_set;
    ds [4] = this->lod_ds->get_descriptor_set (this->frame_index);

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->traverse_octree_pipeline_layout,
        0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->traverse_octree_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, 8, 8, subtree_root_count);
}

void Renderer::traverse_scomtree (VkCommandBuffer cmd_buff) {
    size_t subtree_root_count = this->sdf_scomtree_ds->get_subtree_count ();
    if (!subtree_root_count) {
        return;
    }

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->traverse_scomtree_pipeline);

    std::vector <VkDescriptorSet> ds (5);
    ds [0] = this->sdf_scomtree_ds->get_descriptor_set (this->frame_index);
    ds [1] = this->active_leafs_ds->get_descriptor_set (this->frame_index);
    ds [2] = this->frustum_ds->get_descriptor_set (this->frame_index);
    ds [3] = this->hz_buffer_ds->frame_resources_ref (this->frame_index).descriptor_set;
    ds [4] = this->lod_ds->get_descriptor_set (this->frame_index);

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->traverse_scomtree_pipeline_layout
        , 0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->traverse_scomtree_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, 8, 8, subtree_root_count);
}

void Renderer::prepare_indirect (VkCommandBuffer cmd_buff, uint32_t workgroup_size) {
    assert (this->context && "required for 'prepare_indirect'");
    assert (this->active_leafs_ds && "required for 'prepare_indirect'");
    assert (this->indirect_dispatch_ds && "required for 'prepare_indirect'");

    VkMemoryBarrier barrier1 = {};
    barrier1.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier1.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier1.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , 0
        , 1, &barrier1
        , 0, nullptr
        , 0, nullptr);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prepare_indirect_pipeline);

    std::array <VkDescriptorSet, 2> ds = {
        this->active_leafs_ds->get_descriptor_set (this->frame_index),
        this->indirect_dispatch_ds->get_descriptor_set (this->frame_index),
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prepare_indirect_pipeline_layout,
        0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_prepare_indirect_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (uint32_t), &workgroup_size);

    vkCmdDispatch (cmd_buff, 1, 1, 1);

    VkMemoryBarrier barrier2 = {};
    barrier2.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier2.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier2.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT
        , 0
        , 1, &barrier2
        , 0, nullptr
        , 0, nullptr
        );
}

void Renderer::prepare_hzbuffer_after_forward_rendering (VkCommandBuffer cmd_buff) {
    assert (this->hz_buffer_ds && "required for 'prepare_hzbuffer_after_forward_rendering");
    assert (this->forward_shading && "required for 'prepare_hzbuffer_after_forward_rendering");

    this->hz_buffer_ds->frame_resources_ref (this->frame_index).prev_mvp = this->push_constants.view_proj;

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, .access = 0}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, .access = 0});

    this->copy_forward_rendered_depth (cmd_buff);

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, .stage = VK_PIPELINE_STAGE_TRANSFER_BIT, .access = VK_ACCESS_TRANSFER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT});

    this->compute_hz_buffer (cmd_buff);

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT});
}

void Renderer::marching_cubes_octree (VkCommandBuffer cmd_buff) {
    assert (this->context && "required for 'marching_cubes_octree'");
    assert (this->sdf_octree_ds && "required for 'marching_cubes_octree'");
    assert (this->mesh_ds && "required for 'marching_cubes_octree'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'marching_cubes_octree'");
    assert (this->active_leafs_ds && "required for 'marching_cubes_octree'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'marching_cubes_octree'");

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->marching_cubes_octree_pipeline);

    std::array <VkDescriptorSet, 5> ds = {
        this->sdf_octree_ds->get_descriptor_set (this->frame_index),
        this->mesh_ds->get_descriptor_set (this->frame_index),
        this->marching_cubes_lookup_table_ds->get_descriptor_set (this->frame_index),
        this->active_leafs_ds->get_descriptor_set (this->frame_index),
        this->draw_indexed_indirect_command_ds->get_descriptor_set (this->frame_index)
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->marching_cubes_octree_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->marching_cubes_octree_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatchIndirect (cmd_buff, this->indirect_dispatch_ds->get_indirect_buffer (this->frame_index), 0);
}

void Renderer::marching_cubes_scomtree (VkCommandBuffer cmd_buff) {
    assert (this->context && "required for 'marching_cubes_scomtree'");
    assert (this->sdf_scomtree_ds && "required for 'marching_cubes_scomtree'");
    assert (this->mesh_ds && "required for 'marching_cubes_scomtree'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'marching_cubes_scomtree'");
    assert (this->active_leafs_ds && "required for 'marching_cubes_scomtree'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'marching_cubes_scomtree'");
    assert (this->lod_ds && "required for 'marching_cubes_scomtree'");

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->marching_cubes_scomtree_pipeline);

    std::array <VkDescriptorSet, 6> ds = {
        this->sdf_scomtree_ds->get_descriptor_set (this->frame_index),
        this->mesh_ds->get_descriptor_set (this->frame_index),
        this->marching_cubes_lookup_table_ds->get_descriptor_set (this->frame_index),
        this->active_leafs_ds->get_descriptor_set (this->frame_index),
        this->draw_indexed_indirect_command_ds->get_descriptor_set (this->frame_index),
        this->lod_ds->get_descriptor_set (this->frame_index)
    };

    assert (this->marching_cubes_scomtree_pipeline_layout != VK_NULL_HANDLE);
    assert (this->marching_cubes_scomtree_pipeline != VK_NULL_HANDLE);

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->marching_cubes_scomtree_pipeline_layout
        , 0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->marching_cubes_scomtree_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatchIndirect (cmd_buff, this->indirect_dispatch_ds->get_indirect_buffer (this->frame_index), 0);
}

void Renderer::geometry_barrier (VkCommandBuffer cmd_buff) {
    assert (this->mesh_ds && "required for 'geometry_barrier'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'geometry_barrier'");

    VkBufferMemoryBarrier vertex_buffer_barrier = {};
    vertex_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    vertex_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vertex_buffer_barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vertex_buffer_barrier.buffer = this->mesh_ds->get_vertex_buffer (this->frame_index);
    vertex_buffer_barrier.offset = 0;
    vertex_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier index_buffer_barrier = {};
    index_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    index_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    index_buffer_barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    index_buffer_barrier.buffer = this->mesh_ds->get_index_buffer (this->frame_index);
    index_buffer_barrier.offset = 0;
    index_buffer_barrier.size = VK_WHOLE_SIZE;

    std::array <VkBufferMemoryBarrier, 2> barriers = {vertex_buffer_barrier, index_buffer_barrier};

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0,
        0, nullptr,
        static_cast <uint32_t> (barriers.size ()), barriers.data (),
        0, nullptr
    );

    VkBufferMemoryBarrier indirect_draw_barrier {};
    indirect_draw_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    indirect_draw_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    indirect_draw_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    indirect_draw_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    indirect_draw_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    indirect_draw_barrier.buffer = this->draw_indexed_indirect_command_ds->get_indirect_buffer (this->frame_index);
    indirect_draw_barrier.offset = 0;
    indirect_draw_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0,
        0, nullptr,
        1, &indirect_draw_barrier,
        0, nullptr
    );
}

void Renderer::forward_rendering (VkCommandBuffer cmd_buff) {
    assert (this->context && "required for 'forward_rendering'");
    assert (this->forward_shading && "required for 'forward_rendering'");

    const auto extent = this->render_target->get_extent ();
    const uint32_t image_index = this->render_target->get_current_image_index ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}};
    clear_values [1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->forward_shading->get_render_pass ();
    render_pass_info.framebuffer = this->forward_shading->get_framebuffer (image_index);
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = static_cast <uint32_t> (clear_values.size ());
    render_pass_info.pClearValues = clear_values.data ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    assert (!(this->mesh_ds && !this->draw_indexed_indirect_command_ds) && "either both set (render) or not set (just clear) for 'forward_rendering'");
    assert (!(!this->mesh_ds && this->draw_indexed_indirect_command_ds) && "either both set (render) or not set (just clear) for 'forward_rendering'");

    if (this->mesh_ds && this->draw_indexed_indirect_command_ds) {
        this->set_default_viewport_and_scissor (cmd_buff);

        vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->forward_rendering_pipeline);

        vkCmdPushConstants (cmd_buff, this->forward_rendering_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

        VkBuffer vertex_buffers [] = {this->mesh_ds->get_vertex_buffer (this->frame_index)};
        VkDeviceSize offsets [] = {0};
        vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
        vkCmdBindIndexBuffer (cmd_buff, this->mesh_ds->get_index_buffer (this->frame_index), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect (cmd_buff, this->draw_indexed_indirect_command_ds->get_indirect_buffer (this->frame_index), 0, 1, 0);
    }

    vkCmdEndRenderPass (cmd_buff);
}

void Renderer::draw_frustum_demo (VkCommandBuffer cmd_buff) {
    assert (this->frustum_demo_pipeline && "required for 'draw_frustum_demo'");
    assert (this->frustum_demo_pipeline_layout && "required for 'draw_frustum_demo'");
    assert (this->frustum_draw_buffer && "required for 'draw_frustum_demo'");
    assert (this->render_target && "required for 'draw_frustum_demo'");
    assert ((this->forward_shading || this->deferred_shading) && "required for 'draw_frustum_demo'");

    const auto extent = this->render_target->get_extent ();
    const uint32_t image_index = this->render_target->get_current_image_index ();

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;

    if (this->forward_shading) {
        render_pass_info.renderPass = this->forward_shading->get_render_pass_after ();
        render_pass_info.framebuffer = this->forward_shading->get_framebuffer_after (image_index);
    } else if (this->deferred_shading) {
        render_pass_info.renderPass = this->deferred_shading->get_render_pass_after ();
        render_pass_info.framebuffer = this->deferred_shading->get_framebuffer_after (image_index);
    }

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->frustum_demo_pipeline);

    vkCmdPushConstants (cmd_buff, this->frustum_demo_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &push_constants);

    VkBuffer vertex_buffers [] = { this->frustum_draw_buffer->get_vertex_buffer () };
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->frustum_draw_buffer->get_index_buffer (), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed (cmd_buff, this->frustum_draw_buffer->get_index_count (), 1, 0, 0, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void Renderer::deferred_rendering (VkCommandBuffer cmd_buff) {
    assert (this->context && "required for 'deferred_rendering'");
    assert (this->deferred_shading && "required for 'deferred_rendering'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'deferred_rendering'");
    assert (this->graphics_gbuffer_pipeline != VK_NULL_HANDLE && "required for 'deferred_shading'");
    assert (this->graphics_gbuffer_pipeline_layout != VK_NULL_HANDLE && "required for 'deferred_shading'");
    assert (this->graphics_lighting_pipeline != VK_NULL_HANDLE && "required for 'deferred_shading'");
    assert (this->graphics_lighting_pipeline_layout != VK_NULL_HANDLE && "required for 'deferred_shading'");
    assert (this->mesh_ds && "required for 'deferred_rendering'");

    const auto extent = this->render_target->get_extent ();

    std::array <VkClearValue, 4> gbuffer_clears {};
    gbuffer_clears [0].color = {{0.f, 0.f, 0.f, 0.f}}; // Position
    gbuffer_clears [1].color = {{0.f, 0.f, 0.f, 0.f}}; // Normal
    gbuffer_clears [2].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}}; // Albedo
    gbuffer_clears [3].depthStencil = {1.0f, 0}; // Depth

    VkRenderPassBeginInfo gbuffer_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = this->deferred_shading->get_gbuffer_pass (),
        .framebuffer = this->deferred_shading->get_gbuffer_fb (),
        .renderArea = {{0, 0}, extent},
        .clearValueCount = static_cast <uint32_t> (gbuffer_clears.size ()),
        .pClearValues = gbuffer_clears.data ()
    };

    vkCmdBeginRenderPass (cmd_buff, &gbuffer_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);
    
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphics_gbuffer_pipeline);

    vkCmdPushConstants (cmd_buff, this->graphics_gbuffer_pipeline_layout
                        , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    VkBuffer vertex_buffers [] = {this->mesh_ds->get_vertex_buffer (this->frame_index)};
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->mesh_ds->get_index_buffer (this->frame_index), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect (cmd_buff, this->draw_indexed_indirect_command_ds->get_indirect_buffer (this->frame_index), 0, 1, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void Renderer::calculate_lighting (VkCommandBuffer cmd_buff) {
    const auto extent = this->render_target->get_extent ();
    const uint32_t swap_idx = this->render_target->get_current_image_index ();
    const uint32_t fif_idx = this->frame_index;

    VkClearValue swap_clear {};
    swap_clear.color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}};

    VkRenderPassBeginInfo lighting_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = this->deferred_shading->get_lighting_pass (),
        .framebuffer = this->deferred_shading->get_lighting_fb (swap_idx),
        .renderArea = {{0, 0}, extent},
        .clearValueCount = 1,
        .pClearValues = &swap_clear
    };

    vkCmdBeginRenderPass (cmd_buff, &lighting_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphics_lighting_pipeline);

    std::vector <VkDescriptorSet> ds {};
    ds.push_back (this->deferred_shading->get_descriptor_set ());
    if (this->hz_buffer_ds) {
        ds.push_back (this->hz_buffer_ds->frame_resources_ref (fif_idx).base_level_descriptor_set);
    } else {
        ds.push_back (this->dummy_ds->get_storage_image_ds ());
    }

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS
        , this->graphics_lighting_pipeline_layout, 0, ds.size (), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->graphics_lighting_pipeline_layout
        , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (DeferredLightingPushConstants), &this->deferred_shading->push_constants_ref ());

    vkCmdDraw (cmd_buff, 3, 1, 0, 0); // NOTE: Fullscreen Triangle

    vkCmdEndRenderPass (cmd_buff);
}

void Renderer::raster_octree_via_mesh_shading (VkCommandBuffer cmd_buff) {
    assert (this->sdf_octree_ds && "required for 'raster_octree_via_mesh_shading'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'raster_octree_via_mesh_shading'");
    assert (this->active_leafs_ds && "required for 'raster_octree_via_mesh_shading'");
    assert (this->lod_ds && "required for 'raster_octree_via_mesh_shading'");
    assert (this->indirect_dispatch_ds && "required for 'raster_octree_via_mesh_shading'");
    assert (this->forward_shading && "required for 'raster_octree_via_mesh_shading'"); // TODO: add deferred_shading variant
    assert (this->mesh_shading_octree_pipeline != VK_NULL_HANDLE && "required for 'raster_octree_via_mesh_shading'");
    assert (this->mesh_shading_octree_pipeline_layout != VK_NULL_HANDLE && "required for 'raster_octree_via_mesh_shading'");

    auto original_push_constants = this->push_constants;

    if (this->current_model) {
        using namespace LiteMath;
        float4x4 model = this->current_model->get_model_matrix();
        float4x4 inv_model = LiteMath::inverse4x4(model);

        this->push_constants.view_proj = original_push_constants.view_proj * model;

        float4 local_cam_pos = inv_model * original_push_constants.camera_pos;
        this->push_constants.camera_pos = local_cam_pos;
    }

    this->copy_subtrees (cmd_buff);
    this->reset_active_leafs_counter (cmd_buff);

    this->traverse_octree (cmd_buff);

    this->prepare_indirect (cmd_buff, uint32_t {VOXELS_PER_MESH_WORKGROUP});

    if (this->mesh_shading_octree_pipeline == VK_NULL_HANDLE) {
        if (this->context->get_use_mesh_shading ()) {
            throw std::logic_error ("Mesh shader pipeline is NULL_HANDLE despite mesh shading being supported.");
        }
        this->push_constants = original_push_constants;
        return;
    }

    const auto extent = this->render_target->get_extent ();
    const uint32_t image_index = this->render_target->get_current_image_index ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.0f}};
    clear_values [1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->forward_shading->get_render_pass ();
    render_pass_info.framebuffer = this->forward_shading->get_framebuffer (image_index);
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = static_cast <uint32_t> (clear_values.size ());
    render_pass_info.pClearValues = clear_values.data ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_shading_octree_pipeline);

    std::array <VkDescriptorSet, 4> ds = {
        this->sdf_octree_ds->get_descriptor_set (this->frame_index)
        , this->marching_cubes_lookup_table_ds->get_descriptor_set (this->frame_index)
        , this->active_leafs_ds->get_descriptor_set (this->frame_index)
        , this->lod_ds->get_descriptor_set (this->frame_index)
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_shading_octree_pipeline_layout
        , 0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->mesh_shading_octree_pipeline_layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    assert (sizeof (IndirectDispatch) == sizeof (VkDrawMeshTasksIndirectCommandEXT));
    vkCmdDrawMeshTasksIndirectEXT (cmd_buff, this->indirect_dispatch_ds->get_indirect_buffer (this->frame_index), 0, 1, sizeof (VkDrawMeshTasksIndirectCommandEXT));

    vkCmdEndRenderPass (cmd_buff);

    if (!this->frustum_draw_buffer) {
        this->prepare_hzbuffer_after_forward_rendering (cmd_buff);
    }
}

void Renderer::raster_scomtree_via_mesh_shading (VkCommandBuffer cmd_buff) {
    assert (this->sdf_scomtree_ds && "required for 'raster_scomtree_via_mesh_shading'");
    assert (this->marching_cubes_lookup_table_ds && "required for 'raster_scomtree_via_mesh_shading'");
    assert (this->active_leafs_ds && "required for 'raster_scomtree_via_mesh_shading'");
    assert (this->lod_ds && "required for 'raster_scomtree_via_mesh_shading'");
    assert (this->indirect_dispatch_ds && "required for 'raster_scomtree_via_mesh_shading'");
    assert (this->forward_shading && "required for 'raster_scomtree_via_mesh_shading'"); // TODO: add deferred_shading variant
    assert (this->mesh_shading_scomtree_pipeline != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading'"); // TODO: add deferred_shading variant
    assert (this->mesh_shading_scomtree_pipeline_layout != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading'"); // TODO: add deferred_shading variant

    auto original_push_constants = this->push_constants;

    if (this->current_model) {
        using namespace LiteMath;
        float4x4 model = this->current_model->get_model_matrix();
        float4x4 inv_model = LiteMath::inverse4x4(model);

        this->push_constants.view_proj = original_push_constants.view_proj * model;

        float4 local_cam_pos = inv_model * original_push_constants.camera_pos;
        this->push_constants.camera_pos = local_cam_pos;
    }

    this->copy_subtrees (cmd_buff);
    this->reset_active_leafs_counter (cmd_buff);

    this->traverse_scomtree (cmd_buff);

    this->prepare_indirect (cmd_buff, uint32_t {1}); // NOTE: brick == meshlet

    const auto extent = this->render_target->get_extent ();
    const uint32_t image_index = this->render_target->get_current_image_index ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.0f}};
    clear_values [1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->forward_shading->get_render_pass ();
    render_pass_info.framebuffer = this->forward_shading->get_framebuffer (image_index);
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = static_cast <uint32_t> (clear_values.size ());
    render_pass_info.pClearValues = clear_values.data ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_shading_scomtree_pipeline);

    std::array <VkDescriptorSet, 4> ds = {
        this->sdf_scomtree_ds->get_descriptor_set (this->frame_index)
        , this->marching_cubes_lookup_table_ds->get_descriptor_set (this->frame_index)
        , this->active_leafs_ds->get_descriptor_set (this->frame_index)
        , this->lod_ds->get_descriptor_set (this->frame_index)
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_shading_scomtree_pipeline_layout
        , 0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->mesh_shading_scomtree_pipeline_layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    assert (sizeof (IndirectDispatch) == sizeof (VkDrawMeshTasksIndirectCommandEXT));
    vkCmdDrawMeshTasksIndirectEXT (cmd_buff, this->indirect_dispatch_ds->get_indirect_buffer (this->frame_index), 0, 1, sizeof (VkDrawMeshTasksIndirectCommandEXT));

    vkCmdEndRenderPass (cmd_buff);

    if (!this->frustum_draw_buffer) {
        this->prepare_hzbuffer_after_forward_rendering (cmd_buff);
    }

    this->push_constants = original_push_constants;
}

void Renderer::raster_scomtree_via_mesh_shading_deferred (VkCommandBuffer cmd_buff) {
    assert (this->context && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->deferred_shading && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->draw_indexed_indirect_command_ds && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->mesh_gbuffer_pipeline != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->mesh_gbuffer_pipeline_layout != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->graphics_lighting_pipeline != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->graphics_lighting_pipeline_layout != VK_NULL_HANDLE && "required for 'raster_scomtree_via_mesh_shading_deferred'");
    assert (this->sdf_scomtree_ds && "required for 'raster_scomtree_via_mesh_shading_deferred'");

    this->copy_subtrees (cmd_buff);
    this->reset_active_leafs_counter (cmd_buff);

    this->traverse_scomtree (cmd_buff);

    this->prepare_indirect (cmd_buff, uint32_t {1}); // NOTE: brick == meshlet

        if (!this->frustum_draw_buffer) {
            this->hz_buffer_ds->frame_resources_ref (this->frame_index).prev_mvp = this->push_constants.view_proj;
        }

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});

    const auto extent = this->render_target->get_extent ();
    const uint32_t fif_idx = this->frame_index;
    const uint32_t swap_idx = this->render_target->get_current_image_index ();

    std::array <VkClearValue, 4> gbuffer_clears {};
    gbuffer_clears [0].color = {{0.f, 0.f, 0.f, 0.f}}; // Position
    gbuffer_clears [1].color = {{0.f, 0.f, 0.f, 0.f}}; // Normal
    gbuffer_clears [2].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}}; // Albedo
    gbuffer_clears [3].depthStencil = {1.0f, 0}; // Depth

    VkRenderPassBeginInfo gbuffer_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = this->deferred_shading->get_gbuffer_pass (),
        .framebuffer = this->deferred_shading->get_gbuffer_fb (),
        .renderArea = {{0, 0}, extent},
        .clearValueCount = static_cast <uint32_t> (gbuffer_clears.size ()),
        .pClearValues = gbuffer_clears.data ()
    };

    vkCmdBeginRenderPass (cmd_buff, &gbuffer_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_gbuffer_pipeline);

    std::array <VkDescriptorSet, 4> mesh_ds = {
        this->sdf_scomtree_ds->get_descriptor_set (this->frame_index)
        , this->marching_cubes_lookup_table_ds->get_descriptor_set (this->frame_index)
        , this->active_leafs_ds->get_descriptor_set (this->frame_index)
        , this->lod_ds->get_descriptor_set (this->frame_index)
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_gbuffer_pipeline_layout
        , 0, static_cast <uint32_t> (mesh_ds.size ()), mesh_ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->mesh_gbuffer_pipeline_layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    assert (sizeof (IndirectDispatch) == sizeof (VkDrawMeshTasksIndirectCommandEXT));
    vkCmdDrawMeshTasksIndirectEXT (cmd_buff, this->indirect_dispatch_ds->get_indirect_buffer (this->frame_index), 0, 1, sizeof (VkDrawMeshTasksIndirectCommandEXT));

    vkCmdEndRenderPass (cmd_buff);

    VkClearValue swap_clear {};
    swap_clear.color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}};

    VkRenderPassBeginInfo lighting_pass_info {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = this->deferred_shading->get_lighting_pass (),
        .framebuffer = this->deferred_shading->get_lighting_fb (swap_idx),
        .renderArea = {{0, 0}, extent},
        .clearValueCount = 1,
        .pClearValues = &swap_clear
    };

    vkCmdBeginRenderPass (cmd_buff, &lighting_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    this->set_default_viewport_and_scissor (cmd_buff);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphics_lighting_pipeline);

    std::vector <VkDescriptorSet> gbuffer_ds {};
    gbuffer_ds.push_back (this->deferred_shading->get_descriptor_set ());
    gbuffer_ds.push_back (this->hz_buffer_ds->frame_resources_ref (fif_idx).base_level_descriptor_set);

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS
        , this->graphics_lighting_pipeline_layout, 0, gbuffer_ds.size (), gbuffer_ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->graphics_lighting_pipeline_layout
        , VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (DeferredLightingPushConstants), &this->deferred_shading->push_constants_ref ());

    vkCmdDraw (cmd_buff, 3, 1, 0, 0); // NOTE: Fullscreen Triangle

    vkCmdEndRenderPass (cmd_buff);

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});

    if (!this->frustum_draw_buffer) {
        this->compute_hz_buffer (cmd_buff);
    }

    this->hz_buffer_barrier (cmd_buff
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
        , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
        , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT});
}

void Renderer::raster_octree_via_compute_shading (VkCommandBuffer cmd_buff) {
    auto original_push_constants = this->push_constants;

    if (this->current_model) {
        using namespace LiteMath;
        float4x4 model = this->current_model->get_model_matrix();
        float4x4 inv_model = LiteMath::inverse4x4(model);

        this->push_constants.view_proj = original_push_constants.view_proj * model;

        float4 local_cam_pos = inv_model * original_push_constants.camera_pos;
        this->push_constants.camera_pos = local_cam_pos;
    }

    this->copy_subtrees (cmd_buff);
    this->reset_active_leafs_counter (cmd_buff);

    this->traverse_octree (cmd_buff);

    this->clear_geometry (cmd_buff);
    this->prepare_indirect (cmd_buff, uint32_t {VOXELS_PER_COMPUTE_WORKGROUP});
    this->marching_cubes_octree (cmd_buff);
    this->geometry_barrier (cmd_buff);
    this->forward_rendering (cmd_buff);

    if (!this->frustum_draw_buffer) {
        this->prepare_hzbuffer_after_forward_rendering (cmd_buff);
    }

    this->push_constants = original_push_constants;
}

void Renderer::raster_scomtree_via_compute_shading (VkCommandBuffer cmd_buff) {
    if (this->current_model) {
        LiteMath::float4x4 model = this->current_model->get_model_matrix ();
        this->push_constants.view_proj = this->current_model->get_state ().camera.get_view_projection_matrix () * model;
        
        LiteMath::float4x4 inv_model = LiteMath::inverse4x4 (model);
        LiteMath::float4 camera_pos;
        if (this->frustum_draw_buffer) {
            camera_pos = this->frozen_camera_pos;
        } else {
            camera_pos = LiteMath::to_float4 (this->current_model->get_state ().camera.get_position (), 1.0f);
        }
        this->push_constants.camera_pos = inv_model * camera_pos;
    }

    this->copy_subtrees (cmd_buff);
    this->reset_active_leafs_counter (cmd_buff);

    this->traverse_scomtree (cmd_buff);

    this->clear_geometry (cmd_buff);
    this->prepare_indirect (cmd_buff, uint32_t {BRICKS_PER_COMPUTE_WORKGROUP});
    this->marching_cubes_scomtree (cmd_buff);
    this->geometry_barrier (cmd_buff);

    if (this->deferred_shading) {
        this->deferred_rendering (cmd_buff);

        this->hz_buffer_barrier (cmd_buff
            , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
            , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});
        this->calculate_lighting (cmd_buff);

        this->hz_buffer_barrier (cmd_buff
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT});

        if (!this->frustum_draw_buffer) {
            this->compute_hz_buffer (cmd_buff);
        }

        this->hz_buffer_barrier (cmd_buff
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
            , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT}
            , {.layout = VK_IMAGE_LAYOUT_GENERAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}
            , {.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, .stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, .access = VK_ACCESS_SHADER_READ_BIT});

        if (!this->frustum_draw_buffer) {
            this->hz_buffer_ds->frame_resources_ref (this->frame_index).prev_mvp = this->push_constants.view_proj;
        }
    } else {
        this->forward_rendering (cmd_buff);
        this->prepare_hzbuffer_after_forward_rendering (cmd_buff);
    }
}

void Renderer::render (VkCommandBuffer cmd_buff) {
    if (this->draw) {
        std::invoke (this->draw, this, cmd_buff);
    }

    if (this->frustum_draw_buffer) {
        this->draw_frustum_demo (cmd_buff);
    }
}

const Stats& Renderer::get_stats () const {
    return this->stats;
}

void Renderer::process_commands (std::queue <std::function<void()>>& commands, std::mutex& mutex) {
    std::queue <std::function<void()>> local_commands;
    {
        std::lock_guard lock (mutex);
        if (commands.empty ()) return;
        commands.swap (local_commands);
    }

    if (!local_commands.empty ()) {
        vkDeviceWaitIdle (context->get_device ());
    }

    while (!local_commands.empty ()) {
        auto& cmd_func = local_commands.front ();
        if (cmd_func) {
            cmd_func ();
        }

        local_commands.pop ();
    }
}

void Renderer::release_render_resources () {
    vkDeviceWaitIdle (this->context->get_device ());

    this->current_model.reset ();

    this->active_leafs_ds.reset ();
    this->draw_indexed_indirect_command_ds.reset ();
    this->hz_buffer_ds.reset ();
    this->indirect_dispatch_ds.reset ();
    this->lod_ds.reset ();
    this->marching_cubes_lookup_table_ds.reset ();
    this->mesh_ds.reset ();
    this->sdf_octree_ds.reset ();
    this->sdf_scomtree_ds.reset ();

    this->destroy_pipelines ();

    this->forward_shading.reset ();
    this->deferred_shading.reset ();
}

void Renderer::apply_model_config (std::shared_ptr <Model> model) {
    vkDeviceWaitIdle (this->context->get_device());

    if (model) {
        model->invalidate_cache ();
    }
    this->release_render_resources ();
    this->current_model = model;

    if (!this->current_model) {
        LOG_WARN ("[{}] 'apply_model_config' called with a null scene. Resources cleared.", RENDERER_NAME);
        return;
    }

    const auto method = model->get_state ().draw_method;

    if (method == DrawMethod::ExplicitDeferred || method == DrawMethod::SComTreeComputeDeferred || method == DrawMethod::SComTreeMeshDeferred) {
        this->deferred_shading = std::make_unique <DeferredShading> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_transfer_command_pool_reset ()
            , this->context->get_transfer_queue ()
            , DeferredShadingConfig {
                .gbuffer_formats = { VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM },
                .filter = VK_FILTER_LINEAR
            }
            , this->render_target);
    }

    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeComputeDeferred
        || method == DrawMethod::SComTreeMesh || method == DrawMethod::SComTreeMeshDeferred
        || method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        this->mesh_ds = std::make_unique <MeshDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            , this->push_constants.active_leafs_max_count * MAX_LEAF_VERTS
            , this->push_constants.active_leafs_max_count * MAX_LEAF_PRIMS * 3
            , this->render_target->get_max_frames_in_flight ());
    }

    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeComputeDeferred
        || method == DrawMethod::SComTreeMesh || method == DrawMethod::SComTreeMeshDeferred
        || method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        this->marching_cubes_lookup_table_ds = std::make_unique <MarchingCubesLookupTableDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);

        this->hz_buffer_ds = std::make_unique <HZBufferDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_transfer_command_pool_reset ()
            , this->context->get_transfer_queue ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            , this->render_target->get_extent ()
            , this->render_target->get_max_frames_in_flight ());

        this->indirect_dispatch_ds = std::make_unique <IndirectDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , VK_SHADER_STAGE_COMPUTE_BIT
            , sizeof (IndirectDispatch)
            , 2);

        this->lod_ds = std::make_unique <LODDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_copy_helper ()
            , this->context->get_physical_device ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
            , 1 // TODO: edit to match max models count in scene
            , this->render_target->get_max_frames_in_flight ());

        VkDeviceSize active_leaf_size = (method == DrawMethod::OctreeCompute) ? sizeof (NodeContext) : sizeof (SComTreeBrickPayload);

        this->active_leafs_ds = std::make_unique <ActiveLeafsDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
            , this->push_constants.active_leafs_max_count * active_leaf_size
            , this->render_target->get_max_frames_in_flight ());
    }

    if (method == DrawMethod::SComTreeCompute || method == DrawMethod::SComTreeComputeDeferred
        || method == DrawMethod::SComTreeMesh || method == DrawMethod::SComTreeMeshDeferred
        || method == DrawMethod::OctreeCompute || method == DrawMethod::OctreeMesh) {
        this->draw_indexed_indirect_command_ds = std::make_unique <IndirectDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , VK_SHADER_STAGE_COMPUTE_BIT
            , sizeof (VkDrawIndexedIndirectCommand)
            , this->render_target->get_max_frames_in_flight ());
    }

    if (!this->deferred_shading) {
        this->forward_shading = std::make_unique <ForwardShading> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->render_target);
    }

    if (auto octree_model = std::dynamic_pointer_cast <SdfOctreeModel> (model)) {
        this->sdf_octree_ds = std::make_unique <SdfOctreeDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
            , octree_model
            , this->render_target->get_max_frames_in_flight ()
        );

        const ModelState& model_state = octree_model->get_state ();

        LOG_INFO ("[{}] Created gpu resources for sdf-octree scene '{}'. Depth: {} (cpu: {}, gpu: {})", RENDERER_NAME
             , model_state.name
             , model_state.octree_depth
             , model_state.cpu_traversed
             , model_state.octree_depth - model_state.cpu_traversed);
    } else if (auto obj_model = std::dynamic_pointer_cast <ObjModel> (model)) {
        LOG_INFO ("[{}] Received a scene that of type ObjModel.", RENDERER_NAME);

        const auto& model_data = obj_model->get_model_data ();
        const auto& model_state = obj_model->get_state ();

        if (model_data.vertices.empty ()) {
            LOG_ERROR ("[{}] ObjModel '{}' has no vertices!", RENDERER_NAME, model_state.name);
            return;
        }

        this->mesh_ds = std::make_unique <MeshDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , 0x0 // NOTE: we don't need descriptors at all as we have no plan on using vertex/index buffers as shader input.
            , model_data.vertices.size ()
            , model_data.indices.size ()
            , 1); // NOTE: we don't modify contents of vertex/index buffers each frame, so one set of those buffers is enough.

        this->context->get_copy_helper ()->UpdateBuffer (
            this->mesh_ds->get_vertex_buffer (0), 0
            , model_data.vertices.data (), model_data.vertices.size () * sizeof (Vertex)
        );

        this->context->get_copy_helper ()->UpdateBuffer (
            this->mesh_ds->get_index_buffer (0), 0
            , model_data.indices.data (), model_data.indices.size () * sizeof (uint32_t)
        );

        VkDrawIndexedIndirectCommand cmd {
            .indexCount    = static_cast <uint32_t> (model_data.indices.size ()),
            .instanceCount = 1,
            .firstIndex    = 0,
            .vertexOffset  = 0,
            .firstInstance = 0
        };

        this->draw_indexed_indirect_command_ds = std::make_unique <IndirectDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , VK_SHADER_STAGE_COMPUTE_BIT
            , sizeof (cmd)
            , 1); // NOTE: we don't modify contents of vertex/index buffers each frame, so one set of those buffers is enough.

        this->context->get_copy_helper ()->UpdateBuffer (
            this->draw_indexed_indirect_command_ds->get_indirect_buffer (0), 0
            , &cmd, sizeof (cmd)
        );

        LOG_INFO ("[{}] Created GPU resources for mesh scene '{}'. Vertices: {}, Indices: {}"
            , RENDERER_NAME, model_state.name, model_data.vertices.size (), model_data.indices.size ());
    } else if (auto scomtree_model = std::dynamic_pointer_cast <SComTreeModel> (model)) {
        const auto& model_state = scomtree_model->get_state ();

        this->sdf_scomtree_ds = std::make_unique <SComTreeTreeDescriptorSetInfo> (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
            , scomtree_model
            , this->render_target->get_max_frames_in_flight ()
        );

        LOG_INFO ("[{}] Created gpu resources for sdf-scomtree scene '{}'. Depth: {} (cpu: {}, gpu: {})", RENDERER_NAME
             , model_state.name
             , model_state.octree_depth
             , model_state.cpu_traversed
             , model_state.octree_depth - model_state.cpu_traversed);
    } else {
        LOG_ERROR ("[{}] Received a scene that is not of any renderable type. Cannot render.", RENDERER_NAME);
        return;
    }

    this->create_required_pipelines ();

    auto it = std::find_if (draw_strategies.begin (), draw_strategies.end (), [method] (const MethodTrait& t) { return t.method == method; });
    if (it != draw_strategies.end ()) {
        if (it->needs_mesh_shading && !this->context->get_use_mesh_shading ()) {
            LOG_WARN ("[{}] Mesh shading is not supported", RENDERER_NAME);
            this->draw = nullptr;
        } else {
            this->draw = it->ptr;
        }
    }

    // LOG_INFO ("[{}] Rendering pipeline set to: {}", RENDERER_NAME, this->current_model->get_state ().draw_method);
}

} // namespace sdf_raster

