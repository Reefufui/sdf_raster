#include "sdf_rasterizer.hpp"

#include "application.hpp"
#include "frustum_culling.hpp"
#include "gui.hpp"
#include "logger.hpp"

#include <spdlog/stopwatch.h>
#include <vk_buffers.h>
#include <vk_pipeline.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace sdf_raster {

#define RENDERER_NAME "SDFRasterizer"

SDFRasterizer::SDFRasterizer (std::shared_ptr <VulkanContext> vulkan_context)
    : context (vulkan_context)
    , initialized (false) {
    if (!this->context) {
        throw std::invalid_argument("VulkanContext cannot be null.");
    }
}

SDFRasterizer::~SDFRasterizer () {
}

void SDFRasterizer::init () {
    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error ("VulkanContext is not initialized before renderer init.");
    }

    vk_utils::DescriptorTypesVec ds_type_vec {};
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000);
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000);

    this->descriptor_maker = std::make_shared <vk_utils::DescriptorMaker> (this->context->get_device (), ds_type_vec, 100);
    this->descriptor_maker_for_resizable = std::make_shared <vk_utils::DescriptorMaker> (this->context->get_device (), ds_type_vec, 100);

    this->init_push_constants ();
    this->init_descriptor_sets ();
    this->init_compute_hz_buffer_pipeline ();
    this->init_compute_active_leafs_pipeline ();
    this->init_compute_prepare_indirect_pipeline ();
    this->init_compute_prefix_sum_pass1_pipeline ();
    this->init_compute_prefix_sum_pass2_pipeline ();
    this->init_compute_prefix_sum_pass3_pipeline ();
    this->init_compute_geometry_pipeline ();
    this->init_graphics_shading_pipeline ();
    this->init_graphics_frustum_pipeline ();
    this->register_resizable ();

    if (this->context->get_use_mesh_shading ()) {
        this->init_mesh_shading_pipeline ();
    }

    this->initialized = true;
}

void SDFRasterizer::register_resizable () {
    auto resize_hz_buffer = [&] () {
        cleanup_hz_buffer_descriptor_set (this->context->get_device (), this->hz_buffer_ds);

        VK_CHECK_RESULT (vkResetDescriptorPool (this->context->get_device ()
            , this->descriptor_maker_for_resizable->GetPool ()
            , 0));

        this->hz_buffer_ds = create_hz_buffer_descriptor_set (this->context->get_device ()
            , this->context->get_physical_device ()
            , *(this->descriptor_maker_for_resizable)
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

void SDFRasterizer::init_push_constants () {
    VkPhysicalDeviceProperties device_properties;
    vkGetPhysicalDeviceProperties (this->context->get_physical_device (), &device_properties);
    const uint32_t max_push_constant_size = device_properties.limits.maxPushConstantsSize;
    const uint32_t required_push_constant_size = sizeof (PushConstantsData);
    if (required_push_constant_size > max_push_constant_size) {
        LOG_CRITICAL ("[{}] Required push constants size ({}) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize ({})."
            , RENDERER_NAME, required_push_constant_size, max_push_constant_size);
        throw std::runtime_error ("sizeof (PushConstantsData) exceeds VkPhysicalDeviceProperties::maxPushConstantsSize");
    }

    this->push_constants.active_leafs_max_count = 999999; // TODO: settings
}

void SDFRasterizer::init_descriptor_sets () {
    this->sdf_octree_ds.descriptor_set_layout = vk_utils::createDescriptorSetLayout (this->context->get_device ()
        , {{ 0, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } }, { 1, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } }}
        , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);

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
	    , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT);

	this->active_leafs_ds = create_active_leafs_descriptor_set (this->context->get_device ()
	    , this->context->get_physical_device ()
	    , this->context->get_copy_helper ()
	    , *descriptor_maker
	    , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
	    , this->push_constants.active_leafs_max_count
	    , this->context->get_total_frames ());

    this->draw_indexed_indirect_command_ds = create_draw_indexed_indirect_command_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *descriptor_maker
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->context->get_total_frames ());

    this->hz_buffer_ds = create_hz_buffer_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *(this->descriptor_maker_for_resizable)
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

    this->indirect_dispatch_ds = create_indirect_dispatch_descriptor_set (this->context->get_device ()
        , this->context->get_physical_device ()
        , *descriptor_maker
        , VK_SHADER_STAGE_COMPUTE_BIT
        , this->context->get_total_frames ());
}

void SDFRasterizer::init_compute_hz_buffer_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/mip_max_pooling.comp.slang.spv");
    this->compute_hz_buffer_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
        this->hz_buffer_ds.gen_descriptor_set_layout
        }, 0);
    this->compute_hz_buffer_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void SDFRasterizer::init_compute_active_leafs_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/octree_traversal.comp.slang.spv");
    this->compute_active_leafs_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds.descriptor_set_layout
            , this->active_leafs_ds.descriptor_set_layout
            , this->frustum_ds.descriptor_set_layout
            , this->hz_buffer_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_active_leafs_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void SDFRasterizer::init_compute_prepare_indirect_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/linear_indirect_prep.comp.slang.spv");
    this->compute_prepare_indirect_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
            , this->indirect_dispatch_ds.descriptor_set_layout
        }, sizeof (uint32_t));
    this->compute_prepare_indirect_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void SDFRasterizer::init_compute_prefix_sum_pass1_pipeline () {
    /*
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/prefix_sum_pass1.slang.spv");
    this->compute_prefix_sum_pass1_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass1_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
    */
}

void SDFRasterizer::init_compute_prefix_sum_pass2_pipeline () {
    /*
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/prefix_sum_pass2.slang.spv");
    this->compute_prefix_sum_pass2_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass2_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
    */
}

void SDFRasterizer::init_compute_prefix_sum_pass3_pipeline () {
    /*
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/prefix_sum_pass3.slang.spv");
    this->compute_prefix_sum_pass3_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass3_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
    */
}

void SDFRasterizer::init_compute_geometry_pipeline () {
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "shaders/marching_cubes.comp.slang.spv");
    this->compute_geometry_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds.descriptor_set_layout
            , this->mesh_ds.descriptor_set_layout
            , this->marching_cubes_lookup_table_ds.descriptor_set_layout
            , this->active_leafs_ds.descriptor_set_layout
            , this->draw_indexed_indirect_command_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_geometry_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void SDFRasterizer::init_graphics_shading_pipeline () {
    const size_t shaders_count = 2;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "shaders/identity.vert.slang.spv"
            , VK_SHADER_STAGE_VERTEX_BIT
            , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
            , "shaders/blinn_phong.frag.slang.spv"
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

void SDFRasterizer::init_mesh_shading_pipeline () {
    const size_t shaders_count = 2;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "./shaders/marching_cubes.mesh.slang.spv"
            , VK_SHADER_STAGE_MESH_BIT_EXT
            , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
            , "shaders/blinn_phong.frag.slang.spv"
            , VK_SHADER_STAGE_FRAGMENT_BIT
            , shader_modules);

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof (PushConstantsData);
    pushConstantRange.offset = 0;

    std::vector <VkDescriptorSetLayout> descriptor_set_layouts {};
    descriptor_set_layouts.push_back (this->sdf_octree_ds.descriptor_set_layout);
    descriptor_set_layouts.push_back (this->marching_cubes_lookup_table_ds.descriptor_set_layout);
    descriptor_set_layouts.push_back (this->active_leafs_ds.descriptor_set_layout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = descriptor_set_layouts.size ();
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts.data ();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT (vkCreatePipelineLayout (this->context->get_device (), &pipelineLayoutInfo, nullptr, &this->mesh_pipeline_layout));

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;

    pipelineInfo.layout = this->mesh_pipeline_layout;
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
                , &this->mesh_pipeline));

    for (VkShaderModule module : shader_modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule (this->context->get_device (), module, nullptr);
        }
    }
    shader_modules.clear ();
}

void SDFRasterizer::init_graphics_frustum_pipeline () {
    const size_t shaders_count = 2;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
        , "shaders/frustum_view.vert.slang.spv"
        , VK_SHADER_STAGE_VERTEX_BIT
        , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
        , "shaders/frustum_view.frag.slang.spv"
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

void SDFRasterizer::init_subtree_roots_staging_buffer () {
    this->cleanup_subtree_roots_staging_buffer ();

    VkMemoryRequirements mem_req;
    VkDeviceSize staging_buffer_size = (1LL << (3 * this->cpu_traversed)) * sizeof (NodeContext); // NOTE: max octree nodes on level: pow (8, level)
    this->subtrees_buffer = vk_utils::createBuffer (this->context->get_device (), staging_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &mem_req);

    VkMemoryAllocateInfo allocInfo {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = mem_req.size;
    allocInfo.memoryTypeIndex = vk_utils::findMemoryType (mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, this->context->get_physical_device ());

    VK_CHECK_RESULT (vkAllocateMemory (this->context->get_device (), &allocInfo, nullptr, &this->subtrees_memory));

    vkBindBufferMemory (this->context->get_device (), this->subtrees_buffer, this->subtrees_memory, 0);

    VK_CHECK_RESULT (vkMapMemory (this->context->get_device (), this->subtrees_memory, 0, staging_buffer_size, 0, &this->subtrees_memory_mapped));
}

void SDFRasterizer::update (uint32_t frame_index, const SdfOctree& scene, Settings& settings) {
    this->frame_index = frame_index;

    if (!settings.frustum_view && this->frustum_draw_buffer) {
        this->clear_color = {0.2f, 0.3f, 0.3f, 1.0f};
        settings.camera = this->frustum_draw_buffer->get_camera ();
        this->frustum_draw_buffer.reset ();
        LOG_INFO ("[{}] Frustum view mode: OFF.", RENDERER_NAME);
    } else if (settings.frustum_view && !this->frustum_draw_buffer) {
        this->clear_color = {0.0f, 0.1f, 0.1f, 1.0f};
        this->frustum_draw_buffer = FrustumDrawBuffer::get_frustum_buffer (this->context->get_device ()
            , this->context->get_physical_device ()
            , this->context->get_copy_helper ()
            , settings.camera);
        LOG_INFO ("[{}] Frustum view mode: ON.", RENDERER_NAME);
    }

    if (settings.use_mesh_shading && this->draw_active_leafs != &SDFRasterizer::draw_active_leafs_mesh) {
        if (!this->context->get_use_mesh_shading ()) {
            settings.use_mesh_shading = false;
            LOG_WARN ("[{}] Mesh shader not supported. Falling back to compute shaders (skipping current mesh draw).", RENDERER_NAME);
        } else {
            this->draw_active_leafs = &SDFRasterizer::draw_active_leafs_mesh;
            LOG_INFO ("[{}] Mesh shading: ON.", RENDERER_NAME);
        }
    } else if (!settings.use_mesh_shading && this->draw_active_leafs != &SDFRasterizer::draw_active_leafs_compute) {
        this->draw_active_leafs = &SDFRasterizer::draw_active_leafs_compute;
        LOG_INFO ("[{}] Mesh shading: OFF.", RENDERER_NAME);
    }

    // TODO: separate scene_name update and cpu_traversed update
    if (scene.name != this->scene_name || settings.cpu_traversed != this->cpu_traversed) {
        vkDeviceWaitIdle (this->context->get_device ());

        cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
	    this->sdf_octree_ds = create_sdf_octree_descriptor_set (this->context->get_device ()
	        , this->context->get_physical_device ()
	        , this->context->get_copy_helper ()
	        , *descriptor_maker
	        , VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_MESH_BIT_EXT
	        , scene // TODO: pass nodes without scene name
	        , settings.cpu_traversed
	        , this->context->get_total_frames ());
        this->scene_name = scene.name;

        settings.octree_depth = get_octree_max_depth (scene);
        settings.frustum_culling_level = LiteMath::min (settings.frustum_culling_level, settings.octree_depth);
        settings.occlusion_culling_level = LiteMath::min (settings.frustum_culling_level, settings.octree_depth);

        this->cpu_traversed = settings.cpu_traversed;
        this->subtrees = get_octree_subtrees_payloads (scene, settings.cpu_traversed);
        this->init_subtree_roots_staging_buffer ();

        LOG_INFO ("[{}] Loaded sdf-octree scene '{}'. Depth: {} (cpu: {}, gpu: {})", RENDERER_NAME
             , scene.name
             , settings.octree_depth
             , this->cpu_traversed
             , settings.octree_depth - this->cpu_traversed);
    }

    this->stats.active_leafs_count = fetch_active_leaf_counter (this->context->get_copy_helper (), this->active_leafs_ds, this->frame_index);
    this->stats.active_roots_count = this->visible_subtrees.size ();

    this->push_constants.view_proj = settings.camera.get_view_projection_matrix ();
    this->push_constants.camera_pos = LiteMath::to_float4 (settings.camera.get_position (), 1.0f);
    this->push_constants.prev_view_proj = this->hz_buffer_ds.frame_resources [this->frame_index].prev_view_proj;
    this->push_constants.lod = settings.lod;
    this->push_constants.subtree_root_level = this->cpu_traversed;
    this->push_constants.occlusion_culling_level = settings.occlusion_culling_level;
    this->push_constants.frustum_culling_level = settings.frustum_culling_level;
    this->push_constants.color_leafs = settings.color_leafs;

    FrustumGeometry* ptr = static_cast <FrustumGeometry*> (this->frustum_ds.frustum_geometry_memories_mapped [this->frame_index]);
    if (!settings.frustum_view) {
        this->update_frustum_buffer (settings.camera);
        *ptr = this->frustum;
        frustum_culling (this->subtrees, this->frustum, this->visible_subtrees);
        if (this->visible_subtrees.size ()) {
            memcpy (this->subtrees_memory_mapped, this->visible_subtrees.data (), this->visible_subtrees.size () * sizeof (NodeContext));
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

void SDFRasterizer::update_frustum_buffer (const Camera& camera) {
    const auto& vertices = camera.get_frustum_corners ();
    std::copy (vertices.begin (), vertices.end (), this->frustum.vertices);

    this->frustum.normals [0] = LiteMath::to_float4 (face_normal (this->frustum.vertices [1], this->frustum.vertices [0], this->frustum.vertices [2]), 1.f); // Near
    this->frustum.normals [1] = LiteMath::to_float4 (face_normal (this->frustum.vertices [4], this->frustum.vertices [5], this->frustum.vertices [7]), 1.f); // Far
    this->frustum.normals [2] = LiteMath::to_float4 (face_normal (this->frustum.vertices [0], this->frustum.vertices [4], this->frustum.vertices [6]), 1.f); // Left
    this->frustum.normals [3] = LiteMath::to_float4 (face_normal (this->frustum.vertices [5], this->frustum.vertices [1], this->frustum.vertices [3]), 1.f); // Right
    this->frustum.normals [4] = LiteMath::to_float4 (face_normal (this->frustum.vertices [2], this->frustum.vertices [3], this->frustum.vertices [7]), 1.f); // Top
    this->frustum.normals [5] = LiteMath::to_float4 (face_normal (this->frustum.vertices [4], this->frustum.vertices [0], this->frustum.vertices [1]), 1.f); // Bottom
}

void SDFRasterizer::clear_geometry (VkCommandBuffer cmd_buff) {
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

void SDFRasterizer::copy_depth (VkCommandBuffer cmd_buff) {
    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds.frame_resources [this->frame_index];

    if (!this->frustum_draw_buffer && this->push_constants.occlusion_culling_level) {
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
            .imageExtent = { this->hz_buffer_ds.extent.width, this->hz_buffer_ds.extent.height, 1 }
        };

        vkCmdCopyImageToBuffer (cmd_buff
            , f.prev_depth_image
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
        depth_to_attachment.image = f.prev_depth_image;
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

void SDFRasterizer::copy_subtrees (VkCommandBuffer cmd_buff) {
    VkBufferCopy copy_region = {};
    copy_region.srcOffset = 0;
    copy_region.dstOffset = 0;
    copy_region.size = this->subtrees.size () * sizeof (NodeContext);

    vkCmdCopyBuffer (cmd_buff, this->subtrees_buffer, this->sdf_octree_ds.subtree_root_buffers [this->frame_index], 1, &copy_region);

    VkBufferMemoryBarrier barr = {};
    barr.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barr.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT; 
    barr.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barr.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barr.buffer = this->sdf_octree_ds.subtree_root_buffers [this->frame_index];
    barr.offset = 0;
    barr.size = VK_WHOLE_SIZE;

    vkCmdPipelineBarrier (cmd_buff
        , VK_PIPELINE_STAGE_HOST_BIT
        , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
        , 0
        , 0, nullptr
        , 1, &barr
        , 0, nullptr
    );
}

void SDFRasterizer::compute_hz_buffer (VkCommandBuffer cmd_buff) {
    // NOTE: expects layout of all hz_buffer_ds mip-images to be VK_IMAGE_LAYOUT_GENERAL

    HZBufferDescriptorSetInfo::FrameResources& f = this->hz_buffer_ds.frame_resources [this->frame_index];

    if (this->push_constants.occlusion_culling_level) {
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

void SDFRasterizer::reset_active_leafs_counter (VkCommandBuffer cmd_buff) {
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

void SDFRasterizer::compute_active_leafs (VkCommandBuffer cmd_buff) {
    if (this->visible_subtrees.empty ()) {
        return;
    }

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline);

    std::array <VkDescriptorSet, 4> ds = {
        this->sdf_octree_ds.descriptor_sets [this->frame_index],
        this->active_leafs_ds.descriptor_sets [this->frame_index],
        this->frustum_ds.descriptor_sets [this->frame_index],
        this->hz_buffer_ds.frame_resources [this->frame_index].descriptor_set
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline_layout,
        0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, 8, 8, static_cast <uint32_t> (this->visible_subtrees.size ()));
}

void SDFRasterizer::hz_buffer_barrier (VkCommandBuffer cmd_buff) {
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

void SDFRasterizer::prepare_indirect (VkCommandBuffer cmd_buff, uint32_t workgroup_size) {
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
        this->active_leafs_ds.descriptor_sets [this->frame_index],
        this->indirect_dispatch_ds.descriptor_sets [this->frame_index],
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

void SDFRasterizer::prefix_sum_pass1 (VkCommandBuffer cmd_buff) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline);

    std::array <VkDescriptorSet, 1> ds = {
        this->active_leafs_ds.descriptor_sets [this->frame_index]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    // vkCmdDispatch (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);
}

void SDFRasterizer::prefix_sum_pass2 (VkCommandBuffer) {
    // TODO
}

void SDFRasterizer::prefix_sum_pass3 (VkCommandBuffer) {
    // TODO
}

void SDFRasterizer::compute_geometry (VkCommandBuffer cmd_buff) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline);

    std::array <VkDescriptorSet, 5> ds = {
        this->sdf_octree_ds.descriptor_sets [this->frame_index],
        this->mesh_ds.descriptor_sets [this->frame_index],
        this->marching_cubes_lookup_table_ds.descriptor_set,
        this->active_leafs_ds.descriptor_sets [this->frame_index],
        this->draw_indexed_indirect_command_ds.descriptor_sets [this->frame_index]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_geometry_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatchIndirect (cmd_buff, this->indirect_dispatch_ds.indirect_dispatch_buffers [this->frame_index], 0);
}

void SDFRasterizer::geometry_barrier (VkCommandBuffer cmd_buff) {
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

void SDFRasterizer::draw_geometry (VkCommandBuffer cmd_buff) {
    const auto extent = this->context->get_swapchain_extent ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{this->clear_color.x, this->clear_color.y, this->clear_color.z, 1.f}};
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

    vkCmdPushConstants (cmd_buff, this->graphics_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    VkBuffer vertex_buffers [] = {this->mesh_ds.vertices_buffers [this->frame_index]};
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->mesh_ds.indices_buffers [this->frame_index], 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect (cmd_buff, this->draw_indexed_indirect_command_ds.draw_indexed_indirect_command_buffers [this->frame_index], 0, 1, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void SDFRasterizer::draw_mesh (VkCommandBuffer cmd_buff) {
    if (this->mesh_pipeline == VK_NULL_HANDLE) {
        if (this->context->get_use_mesh_shading ()) {
            throw std::logic_error ("Mesh shader pipeline is NULL_HANDLE despite mesh shading being supported.");
        }
        return;
    }

    const auto extent = this->context->get_swapchain_extent ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
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

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_pipeline);

    std::array <VkDescriptorSet, 3> ds = {
        this->sdf_octree_ds.descriptor_sets [this->frame_index]
        , this->marching_cubes_lookup_table_ds.descriptor_set
        , this->active_leafs_ds.descriptor_sets [this->frame_index]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->mesh_pipeline_layout
        , 0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->mesh_pipeline_layout, VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    assert (sizeof (IndirectDispatch) == sizeof (VkDrawMeshTasksIndirectCommandEXT));
    vkCmdDrawMeshTasksIndirectEXT (cmd_buff, this->indirect_dispatch_ds.indirect_dispatch_buffers [this->frame_index], 0, 1, sizeof (VkDrawMeshTasksIndirectCommandEXT));

    vkCmdEndRenderPass (cmd_buff);
}

void SDFRasterizer::draw_frustum (VkCommandBuffer cmd_buff) {
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

void SDFRasterizer::draw_active_leafs_mesh (VkCommandBuffer cmd_buff) {
    this->prepare_indirect (cmd_buff, uint32_t {VOXELS_PER_MESH_WORKGROUP});
    this->draw_mesh (cmd_buff);
}

void SDFRasterizer::draw_active_leafs_compute (VkCommandBuffer cmd_buff) {
    this->clear_geometry (cmd_buff);
    this->prepare_indirect (cmd_buff, uint32_t {VOXELS_PER_COMPUTE_WORKGROUP});
    this->compute_geometry (cmd_buff);
    this->geometry_barrier (cmd_buff);
    this->draw_geometry (cmd_buff);
}

void SDFRasterizer::render (VkCommandBuffer cmd_buff) {
    assert (this->initialized);

    this->copy_subtrees (cmd_buff);

    if (this->hz_buffer_ds.frame_resources [this->frame_index].prev_depth_image != VK_NULL_HANDLE) {
        this->copy_depth (cmd_buff);
        this->compute_hz_buffer (cmd_buff);
    } else {
        LOG_WARN ("[{}] No previous depth image (likely first/resized frame). Occlusion culling skipped.", RENDERER_NAME);
        this->push_constants.occlusion_culling_level = false;
    }

    this->reset_active_leafs_counter (cmd_buff);
    this->compute_active_leafs (cmd_buff);

    std::invoke (this->draw_active_leafs, this, cmd_buff);

    this->hz_buffer_barrier (cmd_buff);

    if (this->frustum_draw_buffer) {
        this->draw_frustum (cmd_buff);
    } else {
        this->hz_buffer_ds.frame_resources [this->frame_index].prev_depth_image = this->context->get_depth_buffer ().image;
        this->hz_buffer_ds.frame_resources [this->frame_index].prev_view_proj = this->push_constants.view_proj;
    }
}

const Stats& SDFRasterizer::get_stats () {
    return this->stats;
}

void SDFRasterizer::cleanup_subtree_roots_staging_buffer () {
    if (this->subtrees_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->context->get_device (), this->subtrees_buffer, nullptr);
        this->subtrees_buffer = VK_NULL_HANDLE;
    }

    if (this->subtrees_memory != VK_NULL_HANDLE) {
        vkUnmapMemory(this->context->get_device (), this->subtrees_memory);
        vkFreeMemory (this->context->get_device (), this->subtrees_memory, nullptr);
        this->subtrees_memory = VK_NULL_HANDLE;
    }
}

void SDFRasterizer::shutdown () {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->context || !this->context->is_initialized ()) {
        LOG_ERROR ("Vulkan context is already missing");
        return;
    }

    this->cleanup_subtree_roots_staging_buffer ();

    cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
    cleanup_mesh_descriptor_set (this->context->get_device (), this->mesh_ds);
    cleanup_lookup_table_descriptor_set (this->context->get_device (), this->marching_cubes_lookup_table_ds);
    cleanup_active_leafs_descriptor_set (this->context->get_device (), this->active_leafs_ds);
    cleanup_frustum_descriptor_set (this->context->get_device (), this->frustum_ds);
    cleanup_draw_indexed_indirect_command_descriptor_set (this->context->get_device (), this->draw_indexed_indirect_command_ds);
    cleanup_hz_buffer_descriptor_set (this->context->get_device (), this->hz_buffer_ds);
    cleanup_indirect_dispatch_descriptor_set (this->context->get_device (), this->indirect_dispatch_ds);

    if (this->frustum_draw_buffer) {
        this->frustum_draw_buffer.reset ();
    }
    this->descriptor_maker.reset ();
    this->descriptor_maker_for_resizable.reset ();

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_hz_buffer_pipeline, this->compute_hz_buffer_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_active_leafs_pipeline, this->compute_active_leafs_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prepare_indirect_pipeline, this->compute_prepare_indirect_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass1_pipeline, this->compute_prefix_sum_pass1_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass2_pipeline, this->compute_prefix_sum_pass2_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass3_pipeline, this->compute_prefix_sum_pass3_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_geometry_pipeline, this->compute_geometry_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_pipeline, this->graphics_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->mesh_pipeline, this->mesh_pipeline_layout);

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_frustum_pipeline, this->graphics_frustum_pipeline_layout);

    this->initialized = false;
}

} // namespace sdf_raster

