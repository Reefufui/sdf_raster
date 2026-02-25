#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#include "vk_pipeline.h"
#include "compute_shader_renderer.hpp"
#include "application.hpp"
#include "cpu_sandbox/cpu_sandbox.h"
#include "logger.hpp"

namespace {

std::ostream& operator<< (std::ostream& os, const NodeContext& context) {
    os << "NodeContext {"
        << " min_corner: (" << context.min_corner_x << "," << context.min_corner_y << "," << context.min_corner_z << ")"
        << " voxel_size: " << context.voxel_size << ","
        << " node_index: " << context.node_index << ","
        << " cube_index: " << context.cube_index
        << " }";
    return os;
}

}

namespace sdf_raster {

#define RENDERER_NAME "ComputeShaderRenderer"

ComputeShaderRenderer::ComputeShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context)
    : context (vulkan_context)
    , initialized (false) {
    if (!this->context) {
        throw std::invalid_argument("VulkanContext cannot be null.");
    }
}

ComputeShaderRenderer::~ComputeShaderRenderer () {
}

void ComputeShaderRenderer::init (SdfOctree&& a_sdf_octree) {
    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error ("[ComputeShaderRenderer::init] VulkanContext is not initialized before renderer init.");
    }

    if (a_sdf_octree.nodes.size ()) {
        this->sdf_octree = std::move (a_sdf_octree);
    } else {
        throw std::runtime_error ("Missing SDF OCTREE. Make sure './assets/sdf/lowpoly_bunny.octree' is present in launch location");
    }

    this->subtrees = get_octree_subtrees_payloads (this->sdf_octree, 3); // TODO: rebuild each frame

    this->init_push_constants ();
    this->init_descriptor_sets ();
    this->init_compute_hz_buffer_pipeline ();
    this->init_compute_active_leafs_pipeline ();
    this->init_compute_prefix_sum_pass1_pipeline ();
    this->init_compute_prefix_sum_pass2_pipeline ();
    this->init_compute_prefix_sum_pass3_pipeline ();
    this->init_compute_geometry_pipeline ();
    this->init_graphics_shading_pipeline ();

    this->init_graphics_frustum_pipeline ();

    this->register_resizable ();

    this->initialized = true;
    // throw std::runtime_error ("STOP.");
}


void ComputeShaderRenderer::register_resizable () {
    auto resize_hz_buffer = [&] () {
        cleanup_hz_buffer_descriptor_set (this->context->get_device (), this->hz_buffer_ds);

        this->hz_buffer_ds = create_hz_buffer_descriptor_set (this->context->get_device ()
            , this->context->get_physical_device ()
            , *descriptor_maker
            , VK_SHADER_STAGE_COMPUTE_BIT
            , this->context->get_swapchain_extent ()
            , this->context->get_total_frames ());

        change_hz_buffer_layout_to_shader_read_only_optimal (this->context->get_device ()
            , this->context->get_transfer_command_pool_reset ()
            , this->context->get_transfer_queue ()
            , this->hz_buffer_ds);

        LOG_INFO ("[{}] Created {} HZ-buffers for occlusion culling ({}, {}) with {} mip levels.", RENDERER_NAME
            , this->context->get_total_frames ()
            , this->hz_buffer_ds.extent.width, this->hz_buffer_ds.extent.height
            , this->hz_buffer_ds.frame_resources [0].hz_buffer.mipLvls);
    };

    this->context->register_resizable (resize_hz_buffer);
}

void ComputeShaderRenderer::init_push_constants () {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties (this->context->get_physical_device (), &device_properties);
    const uint32_t max_push_constant_size = device_properties.limits.maxPushConstantsSize;
    const uint32_t required_push_constant_size = sizeof (PushConstantsData);
    if (required_push_constant_size > max_push_constant_size) {
        LOG_CRITICAL ("[{}] Required push constants size ({}) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize ({})."
            , RENDERER_NAME, required_push_constant_size, max_push_constant_size);
        throw std::runtime_error ("sizeof (PushConstantsData) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize");
    }

    this->push_constants.active_leafs_max_count = 78240; // TODO: settings

    const auto octree_depth = get_octree_max_depth (this->sdf_octree);
    LOG_INFO ("[{}] Provided sdf-octree's depth: {} levels.", RENDERER_NAME, octree_depth);
    if (octree_depth > MAX_OCTREE_DEPTH) {
        LOG_WARN ("[{}] Provided sdf-octree is too deep. Rendering as if it was {} levels deep.", RENDERER_NAME, uint32_t {MAX_OCTREE_DEPTH});
        this->push_constants.max_octree_depth = MAX_OCTREE_DEPTH;
    } else {
        this->push_constants.max_octree_depth = octree_depth;
    }

    this->push_constants.occlusion_culling = true; // TODO: set inital state from config
    this->push_constants.frustum_culling = true; // TODO: set inital state from config
}

void ComputeShaderRenderer::init_descriptor_sets () {
    vk_utils::DescriptorTypesVec ds_type_vec {};
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000);

    this->descriptor_maker = std::make_shared <vk_utils::DescriptorMaker> (this->context->get_device (), ds_type_vec, 100);

	this->sdf_octree_ds = create_sdf_octree_descriptor_set (this->context->get_device ()
	    , this->context->get_physical_device ()
	    , this->context->get_copy_helper ()
	    , *descriptor_maker
	    , VK_SHADER_STAGE_COMPUTE_BIT
	    , this->sdf_octree
	    , this->subtrees);

	this->mesh_ds = create_mesh_descriptor_set (this->context->get_device ()
	    , this->context->get_physical_device ()
	    , this->context->get_copy_helper ()
	    , *descriptor_maker
	    , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
	    , this->push_constants.active_leafs_max_count * MAX_LEAF_VERTS
	    , this->context->get_total_frames ());

	this->marching_cubes_lookup_table_ds = create_lookup_table_descriptor_set (this->context->get_device ()
	    , this->context->get_physical_device ()
	    , this->context->get_copy_helper ()
	    , *descriptor_maker
	    , VK_SHADER_STAGE_COMPUTE_BIT);

	this->active_leafs_ds = create_active_leafs_descriptor_set (this->context->get_device ()
	    , this->context->get_physical_device ()
	    , this->context->get_copy_helper ()
	    , *descriptor_maker
	    , VK_SHADER_STAGE_COMPUTE_BIT
	    , this->push_constants.active_leafs_max_count
	    , this->context->get_total_frames ());

    this->draw_indexed_indirect_command_ds = create_draw_indexed_indirect_command_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *descriptor_maker
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->context->get_total_frames ());

    this->hz_buffer_ds = create_hz_buffer_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *descriptor_maker
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->context->get_swapchain_extent ()
        , this->context->get_total_frames ());

    change_hz_buffer_layout_to_shader_read_only_optimal (this->context->get_device ()
        , this->context->get_transfer_command_pool_reset ()
        , this->context->get_transfer_queue ()
        , this->hz_buffer_ds);

    LOG_INFO ("[{}] Created {} HZ-buffers for occlusion culling ({}, {}) with {} mip levels.", RENDERER_NAME
        , this->context->get_total_frames ()
        , this->hz_buffer_ds.extent.width, this->hz_buffer_ds.extent.height
        , this->hz_buffer_ds.frame_resources [0].hz_buffer.mipLvls);

    this->frustum_ds = create_frustum_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *descriptor_maker
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->context->get_total_frames ());
}

void ComputeShaderRenderer::init_compute_hz_buffer_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/mip_max_pooling.slang.spv");
    this->compute_hz_buffer_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
        this->hz_buffer_ds.gen_descriptor_set_layout
        }, 0);
    this->compute_hz_buffer_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_active_leafs_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/compute.slang.spv");
    this->compute_active_leafs_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds.descriptor_set_layout
            , this->mesh_ds.descriptor_set_layout
            , this->marching_cubes_lookup_table_ds.descriptor_set_layout
            , this->active_leafs_ds.descriptor_set_layout
            , this->frustum_ds.descriptor_set_layout
            , this->hz_buffer_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_active_leafs_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass1_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass1.slang.spv");
    this->compute_prefix_sum_pass1_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass1_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass2_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass2.slang.spv");
    this->compute_prefix_sum_pass2_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass2_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass3_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass3.slang.spv");
    this->compute_prefix_sum_pass3_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass3_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_geometry_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/triangles_gen.slang.spv");
    this->compute_geometry_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds.descriptor_set_layout
            , this->mesh_ds.descriptor_set_layout
            , this->marching_cubes_lookup_table_ds.descriptor_set_layout
            , this->active_leafs_ds.descriptor_set_layout
            , this->draw_indexed_indirect_command_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_geometry_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_graphics_shading_pipeline () {
    const size_t shaders_count = 2;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/vert.slang.spv"
            , VK_SHADER_STAGE_VERTEX_BIT
            , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/simple_color.slang.spv"
            , VK_SHADER_STAGE_FRAGMENT_BIT
            , shader_modules);

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof (PushConstantsData);
    pushConstantRange.offset = 0;

    std::vector <VkDescriptorSetLayout> descriptor_set_layouts {};
    descriptor_set_layouts.push_back (this->mesh_ds.descriptor_set_layout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = descriptor_set_layouts.size ();
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts.data ();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT (vkCreatePipelineLayout (this->context->get_device (), &pipelineLayoutInfo, nullptr, &this->graphics_pipeline_layout));

    VkVertexInputBindingDescription binding_desc {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof (Vertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector <VkVertexInputAttributeDescription> attributeDescriptions (3);

    // position
    attributeDescriptions [0].binding = 0;
    attributeDescriptions [0].location = 0;
    attributeDescriptions [0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [0].offset = 0;

    // normal
    attributeDescriptions [1].binding = 0;
    attributeDescriptions [1].location = 1;
    attributeDescriptions [1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [1].offset = sizeof (float) * 4;

    // color
    attributeDescriptions [2].binding = 0;
    attributeDescriptions [2].location = 2;
    attributeDescriptions [2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [2].offset = sizeof (float) * 8;

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_layout.vertexBindingDescriptionCount = 1;
    vertex_layout.pVertexBindingDescriptions = &binding_desc;
    vertex_layout.vertexAttributeDescriptionCount = static_cast <uint32_t> (attributeDescriptions.size ());
    vertex_layout.pVertexAttributeDescriptions = attributeDescriptions.data ();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    auto extent = this->context->get_swapchain_extent ();

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) extent.width;
    viewport.height = (float) extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor {};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t) extent.width, (uint32_t) extent.height};

    VkPipelineViewportStateCreateInfo viewportState {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector <VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast <uint32_t> (dynamicStates.size ());
    dynamicState.pDynamicStates = dynamicStates.data ();

    VkGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast <uint32_t> (shader_stages.size ());
    pipelineInfo.pStages = shader_stages.data ();

    pipelineInfo.pVertexInputState = &vertex_layout;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = this->graphics_pipeline_layout;
    if (this->context->get_render_pass () == VK_NULL_HANDLE) {
        throw std::runtime_error ("Render Pass is not initialized!");
    }
    pipelineInfo.renderPass = this->context->get_render_pass ();
    pipelineInfo.subpass = 0;

    VK_CHECK_RESULT (vkCreateGraphicsPipelines (this->context->get_device ()
                , VK_NULL_HANDLE
                , 1
                , &pipelineInfo
                , nullptr
                , &this->graphics_pipeline));

    for (VkShaderModule module : shader_modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule (this->context->get_device (), module, nullptr);
        }
    }
    shader_modules.clear ();
}

void ComputeShaderRenderer::init_graphics_frustum_pipeline () {
    const size_t shaders_count = 2;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/frustum_vert.slang.spv"
            , VK_SHADER_STAGE_VERTEX_BIT
            , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/frustum_frag.slang.spv"
            , VK_SHADER_STAGE_FRAGMENT_BIT
            , shader_modules);

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof (PushConstantsData);
    pushConstantRange.offset = 0;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT (vkCreatePipelineLayout (this->context->get_device (), &pipelineLayoutInfo, nullptr, &this->graphics_frustum_pipeline_layout));

    VkVertexInputBindingDescription binding_desc {};
    binding_desc.binding = 0;
    binding_desc.stride = sizeof (LiteMath::float4);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector <VkVertexInputAttributeDescription> attributeDescriptions (1);
    attributeDescriptions [0].binding = 0;
    attributeDescriptions [0].location = 0;
    attributeDescriptions [0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [0].offset = 0;

    VkPipelineVertexInputStateCreateInfo vertex_layout {};
    vertex_layout.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_layout.vertexBindingDescriptionCount = 1;
    vertex_layout.pVertexBindingDescriptions = &binding_desc;
    vertex_layout.vertexAttributeDescriptionCount = static_cast <uint32_t> (attributeDescriptions.size ());
    vertex_layout.pVertexAttributeDescriptions = attributeDescriptions.data ();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    auto extent = this->context->get_swapchain_extent ();

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) extent.width;
    viewport.height = (float) extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor {};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t) extent.width, (uint32_t) extent.height};

    VkPipelineViewportStateCreateInfo viewportState {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector <VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast <uint32_t> (dynamicStates.size ());
    dynamicState.pDynamicStates = dynamicStates.data ();

    VkGraphicsPipelineCreateInfo pipelineInfo {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast <uint32_t> (shader_stages.size ());
    pipelineInfo.pStages = shader_stages.data ();

    pipelineInfo.pVertexInputState = &vertex_layout;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = this->graphics_frustum_pipeline_layout;
    if (this->context->get_render_pass () == VK_NULL_HANDLE) {
        throw std::runtime_error ("Render Pass is not initialized!");
    }
    pipelineInfo.renderPass = this->context->get_render_pass ();
    pipelineInfo.subpass = 0;

    VK_CHECK_RESULT (vkCreateGraphicsPipelines (this->context->get_device ()
                , VK_NULL_HANDLE
                , 1
                , &pipelineInfo
                , nullptr
                , &this->graphics_frustum_pipeline));

    for (VkShaderModule module : shader_modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule (this->context->get_device (), module, nullptr);
        }
    }
    shader_modules.clear ();
}

void ComputeShaderRenderer::toggle_frustum_buffer (Camera& camera) {
    if (this->frustum_draw_buffer) {
        camera = this->frustum_draw_buffer->get_camera ();
        this->frustum_draw_buffer.reset ();
        return;
    }

    this->frustum_draw_buffer = FrustumDrawBuffer::get_frustum_buffer (this->context->get_device ()
        , this->context->get_physical_device ()
        , this->context->get_copy_helper ()
        , camera);
}

void ComputeShaderRenderer::update_push_constants (const Camera& camera) {
    this->push_constants.view_proj = camera.get_view_projection_matrix ();
    this->push_constants.camera_pos = LiteMath::to_float4 (camera.get_position (), 1.0f);
    this->push_constants.prev_view_proj = this->hz_buffer_ds.frame_resources [this->frame_index].prev_view_proj;

    this->push_constants.occlusion_culling = true; // TODO: ui handle
    this->push_constants.frustum_culling = true; // TODO: ui handle

    // TODO: fix for multiple in flight frames
    if (fetch_active_leaf_overflow_flag (this->context->get_copy_helper (), this->active_leafs_ds)) {
        vkDeviceWaitIdle (this->context->get_device ());
        this->push_constants.max_octree_depth -= 1;
        LOG_WARN ("acitve leaf overflow (exceeds {}).", this->push_constants.active_leafs_max_count);
        LOG_INFO ("sdf-octree's depth permanently reduced: {}->{}", this->push_constants.max_octree_depth + 1, this->push_constants.max_octree_depth);
        if (this->push_constants.max_octree_depth == 0) {
            throw std::runtime_error ("sdf-octree's depth must be greater than zero.");
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

void ComputeShaderRenderer::update_frustum_buffer (const Camera& camera) {
    FrustumGeometry* ptr = static_cast <FrustumGeometry*> (this->frustum_ds.frustum_geometry_memories_mapped [this->frame_index]);

    const auto& vertices = camera.get_frustum_corners ();
    std::copy (vertices.begin (), vertices.end (), ptr->vertices);

    ptr->normals [0] = LiteMath::to_float4 (face_normal (vertices [1], vertices [0], vertices [2]), 1.f); // Near
    ptr->normals [1] = LiteMath::to_float4 (face_normal (vertices [4], vertices [5], vertices [7]), 1.f); // Far
    ptr->normals [2] = LiteMath::to_float4 (face_normal (vertices [0], vertices [4], vertices [6]), 1.f); // Left
    ptr->normals [3] = LiteMath::to_float4 (face_normal (vertices [5], vertices [1], vertices [3]), 1.f); // Right
    ptr->normals [4] = LiteMath::to_float4 (face_normal (vertices [2], vertices [3], vertices [7]), 1.f); // Top
    ptr->normals [5] = LiteMath::to_float4 (face_normal (vertices [4], vertices [0], vertices [1]), 1.f); // Bottom

    static const int edge_indices [12][2] = {
        {0,1}, {1,3}, {3,2}, {2,0}, // Near plane edges
        {4,5}, {5,7}, {7,6}, {6,4}, // Far plane edges
        {0,4}, {1,5}, {2,6}, {3,7}  // Side edges
    };

    for (int i = 0; i < 12; i++) {
        ptr->edges [i] = LiteMath::to_float4 (normalize (LiteMath::to_float3 (vertices [edge_indices [i][1]] - vertices [edge_indices [i][0]])), 1.0f);
    }
}

void ComputeShaderRenderer::clear_geometry (VkCommandBuffer cmd_buff) {
    vkCmdFillBuffer (cmd_buff, this->mesh_ds.vertices_buffers [this->frame_index], 0, VK_WHOLE_SIZE, 0x00000000);
    vkCmdFillBuffer (cmd_buff, this->mesh_ds.indices_buffers [this->frame_index], 0, VK_WHOLE_SIZE, 0x00000000);

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
    barriers [0].buffer = this->mesh_ds.vertices_buffers [this->frame_index];
    barriers [1].buffer = this->mesh_ds.indices_buffers [this->frame_index];

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

void ComputeShaderRenderer::copy_depth (VkCommandBuffer cmd_buff) {
    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds.frame_resources [this->frame_index];
    if (f.prev_depth_image == VK_NULL_HANDLE) {
        LOG_ERROR ("[{}] No previous depth image to copy from.", RENDERER_NAME);
        return;
    }

    VkImageMemoryBarrier depth_to_src = {};
    depth_to_src.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depth_to_src.pNext = nullptr;
    depth_to_src.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depth_to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    depth_to_src.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth_to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    depth_to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depth_to_src.image = f.prev_depth_image;
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
        , 1, &depth_to_src
    );

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
        .imageExtent = { this->hz_buffer_ds.extent.width, this->hz_buffer_ds.extent.height, 1 }
    };

    vkCmdCopyImageToBuffer (cmd_buff
         , f.prev_depth_image
         , VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
         , f.transition_buffer
         , 1, &buffer_image_copy_region);

    VkBufferMemoryBarrier transition_barrier {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
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

    VkImageSubresourceRange first_mip_lvl {};
    first_mip_lvl.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    first_mip_lvl.baseMipLevel = 0;
    first_mip_lvl.levelCount = 1;
    first_mip_lvl.baseArrayLayer = 0;
    first_mip_lvl.layerCount = 1;

    VkImageMemoryBarrier hz_buffer_to_shader_read = {};
    hz_buffer_to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    hz_buffer_to_shader_read.pNext = nullptr;
    hz_buffer_to_shader_read.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hz_buffer_to_shader_read.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    hz_buffer_to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    hz_buffer_to_shader_read.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    hz_buffer_to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hz_buffer_to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hz_buffer_to_shader_read.image = f.hz_buffer.image;
    hz_buffer_to_shader_read.subresourceRange = first_mip_lvl;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_TRANSFER_BIT
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , 0
        , 0, nullptr
        , 0, nullptr
        , 1, &hz_buffer_to_shader_read
    );
}

void ComputeShaderRenderer::compute_hz_buffer (VkCommandBuffer cmd_buff) {
    // NOTE: expects layout of all hz_buffer_ds mip-images to be VK_IMAGE_LAYOUT_GENERAL

    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds.frame_resources [this->frame_index];

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_hz_buffer_pipeline);

    for (uint32_t i = 0; i < f.hz_buffer.mipLvls - 1; ++i) {
        const uint32_t dstMip = i + 1;

        uint32_t dstWidth = std::max (1u, this->hz_buffer_ds.extent.width >> dstMip);
        uint32_t dstHeight = std::max (1u, this->hz_buffer_ds.extent.height >> dstMip);

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

    VkImageSubresourceRange all_mip_lvls;
    all_mip_lvls.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    all_mip_lvls.baseMipLevel = 0;
    all_mip_lvls.levelCount = f.hz_buffer.mipLvls;
    all_mip_lvls.baseArrayLayer = 0;
    all_mip_lvls.layerCount = 1;

    VkImageMemoryBarrier barr = {};
    barr.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barr.pNext = nullptr;
    barr.srcAccessMask = 0;
    barr.dstAccessMask = 0;
    barr.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barr.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.subresourceRange = all_mip_lvls;
    barr.image = f.hz_buffer.image;

    vkCmdPipelineBarrier (cmd_buff
         , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
         , VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
         , 0
         , 0, nullptr
         , 0, nullptr
         , 1, &barr);
}

void ComputeShaderRenderer::reset_active_leafs_counter (VkCommandBuffer cmd_buff) {
    vkCmdFillBuffer (cmd_buff, this->active_leafs_ds.active_leaf_counter_buffers [this->frame_index], 0, VK_WHOLE_SIZE, 0x00000000);

    VkBufferMemoryBarrier buffer_barrier = {};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.pNext = nullptr;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = this->active_leafs_ds.active_leaf_counter_buffers [this->frame_index];
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

void ComputeShaderRenderer::compute_active_leafs (VkCommandBuffer cmd_buff) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline);

    std::array <VkDescriptorSet, 6> ds = {
        this->sdf_octree_ds.descriptor_set, // TODO: sepparate subtree roots buffer for all frames
        this->mesh_ds.descriptor_sets [this->frame_index],
        this->marching_cubes_lookup_table_ds.descriptor_set,
        this->active_leafs_ds.descriptor_sets [this->frame_index],
        this->frustum_ds.descriptor_sets [this->frame_index],
        this->hz_buffer_ds.frame_resources [this->frame_index].descriptor_set
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);
}

void ComputeShaderRenderer::hz_buffer_barrier (VkCommandBuffer cmd_buff) {
    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds.frame_resources [this->frame_index];

    VkImageMemoryBarrier first_lvl_as_depth_dst = {};
    first_lvl_as_depth_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    first_lvl_as_depth_dst.pNext = nullptr;
    first_lvl_as_depth_dst.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    first_lvl_as_depth_dst.dstAccessMask = 0;
    first_lvl_as_depth_dst.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    first_lvl_as_depth_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    first_lvl_as_depth_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    first_lvl_as_depth_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    first_lvl_as_depth_dst.image = f.hz_buffer.image;
    first_lvl_as_depth_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    first_lvl_as_depth_dst.subresourceRange.baseMipLevel = 0;
    first_lvl_as_depth_dst.subresourceRange.levelCount = 1;
    first_lvl_as_depth_dst.subresourceRange.baseArrayLayer = 0;
    first_lvl_as_depth_dst.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
        , 0
        , 0, nullptr
        , 0, nullptr
        , 1, &first_lvl_as_depth_dst
    );

    VkImageMemoryBarrier other_lvls_as_general = {};
    other_lvls_as_general.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    other_lvls_as_general.pNext = nullptr;
    other_lvls_as_general.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    other_lvls_as_general.dstAccessMask = 0;
    other_lvls_as_general.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    other_lvls_as_general.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    other_lvls_as_general.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    other_lvls_as_general.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    other_lvls_as_general.image = f.hz_buffer.image;
    other_lvls_as_general.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    other_lvls_as_general.subresourceRange.baseMipLevel = 1;
    other_lvls_as_general.subresourceRange.levelCount = f.hz_buffer.mipLvls - 1;
    other_lvls_as_general.subresourceRange.baseArrayLayer = 0;
    other_lvls_as_general.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT
        , 0
        , 0, nullptr
        , 0, nullptr
        , 1, &other_lvls_as_general);
}

void ComputeShaderRenderer::active_leafs_barrier (VkCommandBuffer cmd_buff) {
    VkBufferMemoryBarrier active_leafs_buffer_barrier = {};
    active_leafs_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leafs_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leafs_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leafs_buffer_barrier.buffer = this->active_leafs_ds.active_leafs_buffers [this->frame_index];
    active_leafs_buffer_barrier.offset = 0;
    active_leafs_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier active_leaf_vertices_count_buffer_barrier = {};
    active_leaf_vertices_count_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leaf_vertices_count_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_vertices_count_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_vertices_count_buffer_barrier.buffer = this->active_leafs_ds.active_leaf_vertices_count_buffers [this->frame_index];
    active_leaf_vertices_count_buffer_barrier.offset = 0;
    active_leaf_vertices_count_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier active_leaf_indices_count_buffer_barrier = {};
    active_leaf_indices_count_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leaf_indices_count_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_indices_count_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_indices_count_buffer_barrier.buffer = this->active_leafs_ds.active_leaf_indices_count_buffers [this->frame_index];
    active_leaf_indices_count_buffer_barrier.offset = 0;
    active_leaf_indices_count_buffer_barrier.size = VK_WHOLE_SIZE;

    std::array <VkBufferMemoryBarrier, 3> barriers = {
        active_leafs_buffer_barrier
        , active_leaf_vertices_count_buffer_barrier
        , active_leaf_indices_count_buffer_barrier
    };

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        static_cast <uint32_t> (barriers.size ()), barriers.data (),
        0, nullptr
    );

    VkBufferMemoryBarrier indirect_dispatch_barrier {};
    indirect_dispatch_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    indirect_dispatch_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    indirect_dispatch_barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    indirect_dispatch_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    indirect_dispatch_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    indirect_dispatch_barrier.buffer = this->active_leafs_ds.active_leaf_counter_buffers [this->frame_index];
    indirect_dispatch_barrier.offset = 0;
    indirect_dispatch_barrier.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier (
        cmd_buff,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
        0,
        0, nullptr,
        1, &indirect_dispatch_barrier,
        0, nullptr
    );
}

void ComputeShaderRenderer::prefix_sum_pass1 (VkCommandBuffer cmd_buff) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline);

    std::array <VkDescriptorSet, 1> ds = {
        this->active_leafs_ds.descriptor_sets [this->frame_index]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    // vkCmdDispatch (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);
}

void ComputeShaderRenderer::prefix_sum_pass2 (VkCommandBuffer) {
    // TODO
}

void ComputeShaderRenderer::prefix_sum_pass3 (VkCommandBuffer) {
    // TODO
}

void ComputeShaderRenderer::compute_geometry (VkCommandBuffer cmd_buff) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline);

    std::array <VkDescriptorSet, 5> ds = {
        this->sdf_octree_ds.descriptor_set,
        this->mesh_ds.descriptor_sets [this->frame_index],
        this->marching_cubes_lookup_table_ds.descriptor_set,
        this->active_leafs_ds.descriptor_sets [this->frame_index],
        this->draw_indexed_indirect_command_ds.descriptor_sets [this->frame_index]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_geometry_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatchIndirect (cmd_buff, this->active_leafs_ds.active_leaf_counter_buffers [this->frame_index], 0);
}

void ComputeShaderRenderer::geometry_barrier (VkCommandBuffer cmd_buff) {
    VkBufferMemoryBarrier vertex_buffer_barrier = {};
    vertex_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    vertex_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vertex_buffer_barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vertex_buffer_barrier.buffer = this->mesh_ds.vertices_buffers [this->frame_index];
    vertex_buffer_barrier.offset = 0;
    vertex_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier index_buffer_barrier = {};
    index_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    index_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    index_buffer_barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    index_buffer_barrier.buffer = this->mesh_ds.indices_buffers [this->frame_index];
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
    indirect_draw_barrier.buffer = this->draw_indexed_indirect_command_ds.draw_indexed_indirect_command_buffers [this->frame_index];
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

void ComputeShaderRenderer::draw_geometry (VkCommandBuffer cmd_buff) {
    const auto extent = this->context->get_swapchain_extent ();

    std::array <VkClearValue, 2> clear_values {};
    if (this->frustum_draw_buffer) {
        clear_values [0].color = {{0.0f, 0.1f, 0.1f, 1.0f}};
    } else {
        clear_values [0].color = {{0.2f, 0.3f, 0.3f, 1.0f}};
    }
    clear_values [1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->context->get_render_pass ();
    render_pass_info.framebuffer = this->context->get_swapchain_framebuffer ();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;
    render_pass_info.clearValueCount = static_cast <uint32_t> (clear_values.size ());
    render_pass_info.pClearValues = clear_values.data ();

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast <float> (extent.width);
    viewport.height = static_cast <float> (extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport (cmd_buff, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor (cmd_buff, 0, 1, &scissor);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphics_pipeline);

    vkCmdPushConstants (cmd_buff, this->graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &push_constants);

    VkBuffer vertex_buffers [] = {this->mesh_ds.vertices_buffers [this->frame_index]};
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->mesh_ds.indices_buffers [this->frame_index], 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect (cmd_buff, this->draw_indexed_indirect_command_ds.draw_indexed_indirect_command_buffers [this->frame_index], 0, 1, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void ComputeShaderRenderer::draw_frustum (VkCommandBuffer cmd_buff) {
    const auto extent = this->context->get_swapchain_extent ();

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->context->get_render_pass_after ();
    render_pass_info.framebuffer = this->context->get_swapchain_framebuffer_after ();
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;

    vkCmdBeginRenderPass (cmd_buff, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast <float> (extent.width);
    viewport.height = static_cast <float> (extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport (cmd_buff, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor (cmd_buff, 0, 1, &scissor);

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->graphics_frustum_pipeline);

    vkCmdPushConstants (cmd_buff, this->graphics_frustum_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &push_constants);

    VkBuffer vertex_buffers [] = { this->frustum_draw_buffer->get_vertex_buffer () };
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->frustum_draw_buffer->get_index_buffer (), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed (cmd_buff, this->frustum_draw_buffer->get_index_count (), 1, 0, 0, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void ComputeShaderRenderer::render (const Camera& camera) {
    if (!this->initialized) {
        LOG_ERROR ("render called before initialization");
        return;
    }

    auto cmd_buff = this->context->begin_frame (this->frame_index);
    if (cmd_buff == VK_NULL_HANDLE) {
        return;
    }

    this->update_push_constants (camera);
    if (!this->frustum_draw_buffer) {
        this->update_frustum_buffer (camera);
    }

    this->clear_geometry (cmd_buff);

    if (this->push_constants.occlusion_culling) {
        if (this->hz_buffer_ds.frame_resources [this->frame_index].prev_depth_image != VK_NULL_HANDLE) {
            this->copy_depth (cmd_buff);
            this->compute_hz_buffer (cmd_buff);
        } else {
            LOG_WARN ("[{}] No previous depth image (likely first/resized frame). Occlusion culling skipped.", RENDERER_NAME);
            this->push_constants.occlusion_culling = false;
        }
    }

    this->reset_active_leafs_counter (cmd_buff);
    this->compute_active_leafs (cmd_buff);

    this->hz_buffer_barrier (cmd_buff);

    this->active_leafs_barrier (cmd_buff);
    this->compute_geometry (cmd_buff);

    this->geometry_barrier (cmd_buff);
    this->draw_geometry (cmd_buff);

    if (this->frustum_draw_buffer) {
        this->draw_frustum (cmd_buff);
    }

    this->context->end_frame (cmd_buff, this->frame_index);

    static bool dump = true;
    if (!dump) {
        vkDeviceWaitIdle (this->context->get_device ());

        size_t active_leafs_count = fetch_active_leaf_counter (this->context->get_copy_helper (), this->active_leafs_ds, this->frame_index);
        const auto active_leafs = fetch_active_leafs (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, this->frame_index);
        const auto vertices_count = fetch_vertices_count (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, this->frame_index);
        const auto indices_count = fetch_indices_count (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, this->frame_index);
        size_t vertices_count_total = 0;
        size_t indices_count_total = 0;
        for (size_t i = 0; i < active_leafs.size (); ++i) {
            std::cout << "ActiveLeaf ["<< i << "] = " << active_leafs [i] << ", v: " << vertices_count [i] << ", i " << indices_count [i] << std::endl;
            vertices_count_total += vertices_count [i];
            indices_count_total += indices_count [i];
        }
        std::cout << "ActiveLeaf count: " << active_leafs_count << "/" << this->push_constants.active_leafs_max_count << std::endl;
        std::cout << "vertices count: " << vertices_count_total << "/" << 100000 << std::endl;
        std::cout << "indices count: " << indices_count_total << "/" << 100000 << std::endl;

        const auto mesh = fetch_mesh_from_device (this->context->get_copy_helper (), this->mesh_ds, this->frame_index);
        save_mesh_as_obj (mesh, "result.obj");

        dump = true;
        vkDeviceWaitIdle (this->context->get_device ());
    }

    prepare_next_frame_data (this->hz_buffer_ds, this->frame_index, this->context->get_depth_buffer ().image, camera.get_view_projection_matrix ());
    this->frame_index = (this->frame_index + 1) % this->context->get_total_frames ();
}

void ComputeShaderRenderer::shutdown () {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->context || !this->context->is_initialized ()) {
        LOG_ERROR ("Vulkan context is already missing");
        return;
    }

    cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
    cleanup_mesh_descriptor_set (this->context->get_device (), this->mesh_ds);
    cleanup_lookup_table_descriptor_set (this->context->get_device (), this->marching_cubes_lookup_table_ds);
    cleanup_active_leafs_descriptor_set (this->context->get_device (), this->active_leafs_ds);
    cleanup_frustum_descriptor_set (this->context->get_device (), this->frustum_ds);
    cleanup_draw_indexed_indirect_command_descriptor_set (this->context->get_device (), this->draw_indexed_indirect_command_ds);
    cleanup_hz_buffer_descriptor_set (this->context->get_device (), this->hz_buffer_ds);

    if (this->frustum_draw_buffer) {
        this->frustum_draw_buffer.reset ();
    }
    this->descriptor_maker.reset ();

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_hz_buffer_pipeline, this->compute_hz_buffer_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_active_leafs_pipeline, this->compute_active_leafs_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass1_pipeline, this->compute_prefix_sum_pass1_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass2_pipeline, this->compute_prefix_sum_pass2_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass3_pipeline, this->compute_prefix_sum_pass3_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_geometry_pipeline, this->compute_geometry_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_pipeline, this->graphics_pipeline_layout);

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_frustum_pipeline, this->graphics_frustum_pipeline_layout);

    this->initialized = false;
}

} // namespace sdf_raster

