#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>

#include "compute_shader_renderer.hpp"
#include "application.hpp"
#include "cpu_sandbox/cpu_sandbox.h"

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
    a_max_vertices_count = 10000;
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

    std::cout << "[ComputeShaderRenderer::init] subtrees (tasks) count: " << tasks_count << std::endl;
    assert (tasks_count);

    std::cout << "[ComputeShaderRenderer::init] max_vertices_count:" << a_max_vertices_count << std::endl;
    // this->push_constants.max_count_per_task = a_max_vertices_count / tasks_count;
    this->push_constants.max_count_per_task = a_max_vertices_count / tasks_count;
    std::cout << "[ComputeShaderRenderer::init] max vertices per task:" << this->push_constants.max_count_per_task << std::endl;

    // dump_octree_subtree_pretty (this->sdf_octree, this->subtrees [0].node_index, 20, "", 0);
    std::cout << "---" << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].min_corner.x=" << this->subtrees [0].min_corner.x << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].min_corner.y=" << this->subtrees [0].min_corner.y << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].min_corner.z=" << this->subtrees [0].min_corner.z << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].min_corner.voxel_size=" << this->subtrees [0].voxel_size << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].min_corner.node_index=" << this->subtrees [0].node_index << std::endl;
    std::cout << "[ComputeShaderRenderer::init] Subtree [0].cube_index=" << this->subtrees [0].cube_index << std::endl;
    std::cout << "---" << std::endl;

    this->init_descriptor_sets ();
    this->init_compute_shading_pipeline ();
    this->init_graphics_shading_pipeline ();
    this->initialized = true;
    std::cout << "ComputeShaderRenderer initialized successfully." << std::endl;
}

void ComputeShaderRenderer::init_descriptor_sets () {
    vk_utils::DescriptorTypesVec ds_type_vec {};
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000);
    this->descriptor_maker = std::make_shared <vk_utils::DescriptorMaker> (
            this->context->get_device ()
            , ds_type_vec
            , 10
            );
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
			, this->push_constants.max_count_per_task
			, this->context->get_total_frames ());
	this->marching_cubes_lookup_table_ds = create_lookup_table_descriptor_set (this->context->get_device ()
			, this->context->get_physical_device ()
			, this->context->get_copy_helper ()
			, *descriptor_maker
			, VK_SHADER_STAGE_COMPUTE_BIT);
}

void ComputeShaderRenderer::init_compute_shading_pipeline () {
    std::cout << "ComputeShaderRenderer::init_compute_shading_pipeline called." << std::endl;

    const size_t shaders_count = 1;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/compute.slang.spv"
            , VK_SHADER_STAGE_COMPUTE_BIT
            , shader_modules);

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.size = sizeof (PushConstantsData);
    pushConstantRange.offset = 0;

    std::vector <VkDescriptorSetLayout> descriptor_set_layouts {};
    descriptor_set_layouts.push_back (this->sdf_octree_ds.descriptor_set_layout);
    descriptor_set_layouts.push_back (this->mesh_ds.descriptor_set_layout);
    descriptor_set_layouts.push_back (this->marching_cubes_lookup_table_ds.descriptor_set_layout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = descriptor_set_layouts.size ();
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts.data ();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT (vkCreatePipelineLayout (this->context->get_device (), &pipelineLayoutInfo, nullptr, &this->compute_pipeline_layout));

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shader_stages [0];
    pipelineInfo.layout = this->compute_pipeline_layout;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VK_CHECK_RESULT (vkCreateComputePipelines (this->context->get_device ()
                , VK_NULL_HANDLE
                , 1
                , &pipelineInfo
                , nullptr
                , &this->compute_pipeline));

    for (VkShaderModule module : shader_modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule (this->context->get_device (), module, nullptr);
        }
    }
    shader_modules.clear ();
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
    bindingDescription.stride = sizeof (float) * (4 + 4);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector <VkVertexInputAttributeDescription> attributeDescriptions (2);

    attributeDescriptions [0].binding = 0;
    attributeDescriptions [0].location = 0;
    attributeDescriptions [0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [0].offset = 0;

    attributeDescriptions [1].binding = 0;
    attributeDescriptions [1].location = 1;
    attributeDescriptions [1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributeDescriptions [1].offset = sizeof (float) * 4;

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
    viewport.maxDepth = 10000.0f;

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
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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

void ComputeShaderRenderer::render (const Camera& a_camera) {
    if (!this->initialized) {
        std::cerr << "Warning: MeshShaderRenderer::render called before init()." << std::endl;
        return;
    }

    auto cmd_buff = this->context->begin_frame ();
    if (cmd_buff == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_pipeline);

    const auto current_frame = this->context->get_current_frame ();
    std::array <VkDescriptorSet, 3> compute_descriptor_sets = {
        this->sdf_octree_ds.descriptor_set,
        this->mesh_ds.descriptor_sets [current_frame],
        this->marching_cubes_lookup_table_ds.descriptor_set
    };

    vkCmdBindDescriptorSets (cmd_buff, VK_PIPELINE_BIND_POINT_COMPUTE, this->compute_pipeline_layout,
                             0, static_cast <uint32_t> (compute_descriptor_sets.size ()), compute_descriptor_sets.data (), 0, nullptr);

    update_push_constants (a_camera);
    vkCmdPushConstants (cmd_buff, this->compute_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (PushConstantsData), &this->push_constants);

    vkCmdDispatch (cmd_buff, 1, 1, 1);

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

    const auto extent = this->context->get_swapchain_extent ();

    VkRenderPassBeginInfo render_pass_info {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass = this->context->get_render_pass ();
    render_pass_info.framebuffer = this->context->get_swapchain_framebuffer (this->context->get_current_image_index ());
    render_pass_info.renderArea.offset = {0, 0};
    render_pass_info.renderArea.extent = extent;

    VkClearValue clear_color = {{{0.2f, 0.3f, 0.3f, 1.0f}}};
    render_pass_info.clearValueCount = 1;
    render_pass_info.pClearValues = &clear_color;

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

    uint32_t index_count = 3;
    // TODO: uint32_t actual_index_count = fetch_actual_index_count_from_gpu_buffer (cmd_buff, this->mesh_ds.indices_count_buffer [current_frame]);

    vkCmdDrawIndexed (cmd_buff, index_count, 1, 0, 0, 0);

    vkCmdEndRenderPass (cmd_buff);

    this->context->end_frame (cmd_buff);
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

    // std::cout << "[ComputeShaderRenderer::shutdown] Fetching last frame leaf contexts..." << std::endl;
    // std::vector <LeafContext> leaf_contexts = fetch_leaf_contexts (this->context->get_copy_helper (), this->sdf_octree_ds, this->max_vertices_count, this->context->get_current_frame ());
    // dump_active_leafs (leaf_contexts, "contexts.txt", this->max_vertices_count / sizeof (LeafContext) / this->subtrees.size ());

    cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
    cleanup_mesh_descriptor_set (this->context->get_device (), this->mesh_ds);
    cleanup_lookup_table_descriptor_set (this->context->get_device (), this->marching_cubes_lookup_table_ds);

    this->descriptor_maker.reset ();

    if (this->compute_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline (this->context->get_device (), this->compute_pipeline, nullptr);
        this->compute_pipeline = VK_NULL_HANDLE;
    }
    if (this->compute_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout (this->context->get_device (), this->compute_pipeline_layout, nullptr);
        this->compute_pipeline_layout = VK_NULL_HANDLE;
    }

    if (this->graphics_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline (this->context->get_device (), this->graphics_pipeline, nullptr);
        this->graphics_pipeline = VK_NULL_HANDLE;
    }
    if (this->graphics_pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout (this->context->get_device (), this->graphics_pipeline_layout, nullptr);
        this->graphics_pipeline_layout = VK_NULL_HANDLE;
    }

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

