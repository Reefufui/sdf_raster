#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#include "vk_pipeline.h"
#include "compute_shader_renderer.hpp"
#include "application.hpp"
#include "cpu_sandbox/cpu_sandbox.h"

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

ComputeShaderRenderer::ComputeShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context)
    : context (vulkan_context)
    , width (0)
    , height (0)
    , initialized (false) {
    if (!this->context) {
        throw std::invalid_argument("VulkanContext cannot be null.");
    }
    std::cout << "ComputeShaderRenderer created." << std::endl;
}

ComputeShaderRenderer::~ComputeShaderRenderer () {
    std::cout << "ComputeShaderRenderer destroyed." << std::endl;
}

void ComputeShaderRenderer::init (int a_width, int a_height, SdfOctree&& a_sdf_octree, size_t a_max_vertices_count) {
    std::cout << "ComputeShaderRenderer initializing..." << std::endl;

    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error ("[ComputeShaderRenderer::init] VulkanContext is not initialized before renderer init.");
    }

    this->width = a_width;
    this->height = a_height;
    if (a_sdf_octree.nodes.size ()) {
        this->sdf_octree = std::move (a_sdf_octree);
    } else {
        throw std::runtime_error ("Missing SDF OCTREE. Make sure './assets/sdf/lowpoly_bunny.octree' is present in launch location");
    }
    this->subtrees = get_octree_subtrees_payloads (this->sdf_octree, 3);
    this->push_constants.max_octree_depth = get_octree_max_depth (this->sdf_octree, MAX_OCTREE_DEPTH);
    std::cout << "[ComputeShaderRenderer::init] MAX_OCTREE_DEPTH: " << MAX_OCTREE_DEPTH << std::endl;
    std::cout << "[ComputeShaderRenderer::init] given sdf's depth: " << this->push_constants.max_octree_depth << std::endl;
    if (this->push_constants.max_octree_depth > MAX_OCTREE_DEPTH) {
        std::cout << "[ComputeShaderRenderer::init] given octree is too deep. Reducing it to MAX_OCTREE_DEPTH" << std::endl;
        this->push_constants.max_octree_depth = MAX_OCTREE_DEPTH;
    }

    std::cout << "[ComputeShaderRenderer::init] sizeof (PushConstantsData): " << sizeof (PushConstantsData) << std::endl;

    const int tasks_count = this->subtrees.size ();
    std::cout << "[ComputeShaderRenderer::init] Subtree count: " << tasks_count << std::endl;
    assert (tasks_count);

    this->push_constants.active_leafs_max_count = 778240;

    this->init_descriptor_sets ();
    this->init_compute_active_leafs_pipeline ();
    this->init_compute_prefix_sum_pass1_pipeline ();
    this->init_compute_prefix_sum_pass2_pipeline ();
    this->init_compute_prefix_sum_pass3_pipeline ();
    this->init_compute_geometry_pipeline ();
    this->init_graphics_shading_pipeline ();

    this->initialized = true;
    std::cout << "ComputeShaderRenderer initialized successfully." << std::endl;
}

void ComputeShaderRenderer::init_descriptor_sets () {
    vk_utils::DescriptorTypesVec ds_type_vec {};
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000);
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
}

void ComputeShaderRenderer::init_compute_active_leafs_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_active_leafs_pipeline called." << std::endl;
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/compute.slang.spv");
    this->compute_active_leafs_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->sdf_octree_ds.descriptor_set_layout
            , this->mesh_ds.descriptor_set_layout
            , this->marching_cubes_lookup_table_ds.descriptor_set_layout
            , this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_active_leafs_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass1_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_prefix_sum_pass1 pipeline called." << std::endl;
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass1.slang.spv");
    this->compute_prefix_sum_pass1_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass1_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass2_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_prefix_sum_pass2_pipeline pipeline called." << std::endl;
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass2.slang.spv");
    this->compute_prefix_sum_pass2_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass2_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_prefix_sum_pass3_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_prefix_sum_pass3_pipeline called." << std::endl;
    vk_utils::ComputePipelineMaker compute_pipeline_maker;
    compute_pipeline_maker.LoadShader (this->context->get_device (), "./assets/shaders/prefix_sum_pass3.slang.spv");
    this->compute_prefix_sum_pass3_pipeline_layout = compute_pipeline_maker.MakeLayout (this->context->get_device (), {
            this->active_leafs_ds.descriptor_set_layout
        }, sizeof (PushConstantsData));
    this->compute_prefix_sum_pass3_pipeline = compute_pipeline_maker.MakePipeline (this->context->get_device ());
}

void ComputeShaderRenderer::init_compute_geometry_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_prefix_sum_pipeline called." << std::endl;
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
    std::cout << "ComputeShaderRenderer::init_graphics_shading_pipeline called." << std::endl;

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

    VkVertexInputBindingDescription bindingDescription {};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof (Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast <uint32_t> (attributeDescriptions.size ());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data ();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) this->width;
    viewport.height = (float) this->height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor {};
    scissor.offset = {0, 0};
    scissor.extent = {(uint32_t) this->width, (uint32_t) this->height};

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

void ComputeShaderRenderer::reset_active_leafs_counter (VkCommandBuffer cmd_buff, size_t current_frame) {
    vkCmdFillBuffer (cmd_buff, this->active_leafs_ds.active_leaf_counter_buffers [current_frame], 0, VK_WHOLE_SIZE, 0x00000000);

    VkBufferMemoryBarrier buffer_barrier = {};
    buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_barrier.pNext = nullptr;
    buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_barrier.buffer = this->active_leafs_ds.active_leaf_counter_buffers [current_frame];
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

void ComputeShaderRenderer::clear_geometry (VkCommandBuffer cmd_buff, size_t current_frame) {
    vkCmdFillBuffer (cmd_buff, this->mesh_ds.vertices_buffers [current_frame], 0, VK_WHOLE_SIZE, 0x00000000);
    vkCmdFillBuffer (cmd_buff, this->mesh_ds.indices_buffers [current_frame], 0, VK_WHOLE_SIZE, 0x00000000);

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
    barriers [0].buffer = this->mesh_ds.vertices_buffers [current_frame];
    barriers [1].buffer = this->mesh_ds.indices_buffers [current_frame];

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

void ComputeShaderRenderer::compute_active_leafs (VkCommandBuffer cmd_buff, size_t current_frame) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline);

    std::array <VkDescriptorSet, 4> ds = {
        this->sdf_octree_ds.descriptor_set,
        this->mesh_ds.descriptor_sets [current_frame],
        this->marching_cubes_lookup_table_ds.descriptor_set,
        this->active_leafs_ds.descriptor_sets [current_frame]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_active_leafs_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);
}

void ComputeShaderRenderer::active_leafs_barrier (VkCommandBuffer cmd_buff, size_t current_frame) {
    VkBufferMemoryBarrier active_leafs_buffer_barrier = {};
    active_leafs_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leafs_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leafs_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leafs_buffer_barrier.buffer = this->active_leafs_ds.active_leafs_buffers [current_frame];
    active_leafs_buffer_barrier.offset = 0;
    active_leafs_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier active_leaf_vertices_count_buffer_barrier = {};
    active_leaf_vertices_count_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leaf_vertices_count_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_vertices_count_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_vertices_count_buffer_barrier.buffer = this->active_leafs_ds.active_leaf_vertices_count_buffers [current_frame];
    active_leaf_vertices_count_buffer_barrier.offset = 0;
    active_leaf_vertices_count_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier active_leaf_indices_count_buffer_barrier = {};
    active_leaf_indices_count_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    active_leaf_indices_count_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_indices_count_buffer_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    active_leaf_indices_count_buffer_barrier.buffer = this->active_leafs_ds.active_leaf_indices_count_buffers [current_frame];
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
    indirect_dispatch_barrier.buffer = this->active_leafs_ds.active_leaf_counter_buffers [current_frame];
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

void ComputeShaderRenderer::prefix_sum_pass1 (VkCommandBuffer cmd_buff, size_t current_frame) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline);

    std::array <VkDescriptorSet, 1> ds = {
        this->active_leafs_ds.descriptor_sets [current_frame]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_prefix_sum_pass1_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_active_leafs_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    // vkCmdDispatch (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);
}

void ComputeShaderRenderer::prefix_sum_pass2 (VkCommandBuffer cmd_buff, size_t current_frame) {
}

void ComputeShaderRenderer::prefix_sum_pass3 (VkCommandBuffer cmd_buff, size_t current_frame) {
}

void ComputeShaderRenderer::compute_geometry (VkCommandBuffer cmd_buff, size_t current_frame) {
    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline);

    std::array <VkDescriptorSet, 5> ds = {
        this->sdf_octree_ds.descriptor_set,
        this->mesh_ds.descriptor_sets [current_frame],
        this->marching_cubes_lookup_table_ds.descriptor_set,
        this->active_leafs_ds.descriptor_sets [current_frame],
        this->draw_indexed_indirect_command_ds.descriptor_sets [current_frame]
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_geometry_pipeline_layout,
                             0, static_cast <uint32_t> (ds.size ()), ds.data (), 0, nullptr);

    vkCmdPushConstants (cmd_buff, this->compute_geometry_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatchIndirect (cmd_buff, this->active_leafs_ds.active_leaf_counter_buffers [current_frame], 0);
}

void ComputeShaderRenderer::geometry_barrier (VkCommandBuffer cmd_buff, size_t current_frame) {
    VkBufferMemoryBarrier vertex_buffer_barrier = {};
    vertex_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    vertex_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vertex_buffer_barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vertex_buffer_barrier.buffer = this->mesh_ds.vertices_buffers [current_frame];
    vertex_buffer_barrier.offset = 0;
    vertex_buffer_barrier.size = VK_WHOLE_SIZE;

    VkBufferMemoryBarrier index_buffer_barrier = {};
    index_buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    index_buffer_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    index_buffer_barrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    index_buffer_barrier.buffer = this->mesh_ds.indices_buffers [current_frame];
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
    indirect_draw_barrier.buffer = this->draw_indexed_indirect_command_ds.draw_indexed_indirect_command_buffers [current_frame];
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

void ComputeShaderRenderer::draw_geometry (VkCommandBuffer cmd_buff, size_t current_frame) {
    const auto extent = this->context->get_swapchain_extent ();

    std::array <VkClearValue, 2> clear_values {};
    clear_values [0].color = {{0.2f, 0.3f, 0.3f, 1.0f}};
    clear_values [1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->context->get_render_pass ();
    render_pass_info.framebuffer = this->context->get_swapchain_framebuffer (this->context->get_current_image_index ());
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

    VkBuffer vertex_buffers [] = {this->mesh_ds.vertices_buffers [current_frame]};
    VkDeviceSize offsets [] = {0};
    vkCmdBindVertexBuffers (cmd_buff, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer (cmd_buff, this->mesh_ds.indices_buffers [current_frame], 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexedIndirect (cmd_buff, this->draw_indexed_indirect_command_ds.draw_indexed_indirect_command_buffers [current_frame], 0, 1, 0);

    vkCmdEndRenderPass (cmd_buff);
}

void ComputeShaderRenderer::render (const Camera& a_camera) {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->initialized) {
        std::cerr << "Warning: MeshShaderRenderer::render called before init()." << std::endl;
        return;
    }

    auto cmd_buff = this->context->begin_frame ();
    if (cmd_buff == VK_NULL_HANDLE) {
        return;
    }

    const auto current_frame = this->context->get_current_frame ();

    this->update_push_constants (a_camera);
    this->reset_active_leafs_counter (cmd_buff, current_frame);
    this->clear_geometry (cmd_buff, current_frame);
    this->compute_active_leafs (cmd_buff, current_frame);
    this->active_leafs_barrier (cmd_buff, current_frame);
    // this->prefix_sum_pass1 (cmd_buff, current_frame);
    // this->prefix_sum_pass2 (cmd_buff, current_frame);
    // this->prefix_sum_pass3 (cmd_buff, current_frame);
    this->compute_geometry (cmd_buff, current_frame);
    this->geometry_barrier (cmd_buff, current_frame);
    this->draw_geometry (cmd_buff, current_frame);

    this->context->end_frame (cmd_buff);

    static bool dump = false;
    if (!dump) {
        vkDeviceWaitIdle (this->context->get_device ());

        size_t active_leafs_count = fetch_active_leaf_counter (this->context->get_copy_helper (), this->active_leafs_ds, current_frame);
        const auto active_leafs = fetch_active_leafs (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, current_frame);
        const auto vertices_count = fetch_vertices_count (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, current_frame);
        const auto indices_count = fetch_indices_count (this->context->get_copy_helper (), this->active_leafs_ds, active_leafs_count, current_frame);
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

        const auto mesh = fetch_mesh_from_device (this->context->get_copy_helper (), this->mesh_ds, current_frame);
        save_mesh_as_obj (mesh, "result.obj");

        dump = true;
        vkDeviceWaitIdle (this->context->get_device ());
    }
}

void ComputeShaderRenderer::resize (int a_width, int a_height) {
    if (!this->initialized) {
        std::cerr << "Warning: ComputeShaderRenderer::resize called before init()." << std::endl;
        this->width = a_width;
        this->height = a_height;
        return;
    }

    std::cout << "ComputeShaderRenderer resizing to " << a_width << "x" << a_height << "..." << std::endl;

    this->width = a_width;
    this->height = a_height;
}

void ComputeShaderRenderer::shutdown () {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->context || !this->context->is_initialized ()) {
        std::cerr << "[ComputeShaderRenderer::shutdown] Warning: Vulkan context is already missing." << std::endl;
        return;
    }

    std::cout << "[ComputeShaderRenderer::shutdown] ComputeShaderRenderer shutting down..." << std::endl;

    cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
    cleanup_mesh_descriptor_set (this->context->get_device (), this->mesh_ds);
    cleanup_lookup_table_descriptor_set (this->context->get_device (), this->marching_cubes_lookup_table_ds);
    cleanup_active_leafs_descriptor_set (this->context->get_device (), this->active_leafs_ds);
    cleanup_draw_indexed_indirect_command_descriptor_set (this->context->get_device (), this->draw_indexed_indirect_command_ds);

    this->descriptor_maker.reset ();

    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_active_leafs_pipeline, this->compute_active_leafs_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass1_pipeline, this->compute_prefix_sum_pass1_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass2_pipeline, this->compute_prefix_sum_pass2_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_prefix_sum_pass3_pipeline, this->compute_prefix_sum_pass3_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->compute_geometry_pipeline, this->compute_geometry_pipeline_layout);
    vk_utils::destroyPipelineIfExists (this->context->get_device (), this->graphics_pipeline, this->graphics_pipeline_layout);

    this->render_pass = VK_NULL_HANDLE;

    this->initialized = false;
}

void ComputeShaderRenderer::update_push_constants (const Camera& a_camera) {
    float aspect_ratio = static_cast <float> (this->width) / static_cast <float> (this->height);
    this->push_constants.view_proj = a_camera.get_view_projection_matrix (aspect_ratio);
    this->push_constants.camera_pos = LiteMath::to_float4 (a_camera.camera_position, 1.f);
    this->push_constants.color = LiteMath::float4 (0.f, 1.f, 0.f, 1.f);

    std::vector <LiteMath::float4> planes = Camera::extract_frustum_planes (push_constants.view_proj);
    for (int i = 0; i < 6; ++i) {
        this->push_constants.frustum_planes [i] = planes [i];
    }

    uint insufficent_mem_flag = fetch_insufficent_mem_flag (this->context->get_copy_helper (), this->mesh_ds);
    if (insufficent_mem_flag) {
        vkDeviceWaitIdle (this->context->get_device ());
        this->push_constants.max_octree_depth -= 1;
        std::cout << "[ComputeShaderRenderer::update_push_constants]: insufficent_mem_flag : " << insufficent_mem_flag << ". Reducing octree depth from "
            << this->push_constants.max_octree_depth + 1 << " to " << this->push_constants.max_octree_depth << std::endl;
        if (this->push_constants.max_octree_depth == 0) {
            throw std::runtime_error ("[ComputeShaderRenderer::update_push_constants]: bug. octree must not me 0 depth");
        }
    }
}

} // namespace sdf_raster

