#include <stdexcept>
#include <array>

#include "mesh_shader_renderer.hpp"
#include "application.hpp"
#include "cpu_sandbox/cpu_sandbox.h"

namespace sdf_raster {

MeshShaderRenderer::MeshShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context)
    : context (vulkan_context)
    , width (0)
    , height (0)
    , initialized (false) {
    if (!this->context) {
        throw std::invalid_argument("VulkanContext cannot be null.");
    }
    std::cout << "MeshShaderRenderer created." << std::endl;
}

MeshShaderRenderer::~MeshShaderRenderer () {
    std::cout << "MeshShaderRenderer destroyed." << std::endl;
}

void MeshShaderRenderer::init (int a_width, int a_height, SdfOctree&& a_sdf_octree, size_t a_leaf_memory_limit) {
    std::cout << "MeshShaderRenderer initializing..." << std::endl;

    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error ("[MeshShaderRenderer::init] VulkanContext is not initialized before renderer init.");
    }

    this->width = a_width;
    this->height = a_height;
    this->sdf_octree = std::move (a_sdf_octree);
    this->subtrees = get_octree_subtrees_payloads (this->sdf_octree, 3);
    this->push_constants.max_octree_depth = get_octree_max_depth (this->sdf_octree, MAX_OCTREE_DEPTH);
    std::cout << "[MeshShaderRenderer::init] MAX_OCTREE_DEPTH: " << MAX_OCTREE_DEPTH << std::endl;
    std::cout << "[MeshShaderRenderer::init] given sdf's depth: " << this->push_constants.max_octree_depth << std::endl;
    if (this->push_constants.max_octree_depth > MAX_OCTREE_DEPTH) {
        std::cout << "[MeshShaderRenderer::init] given octree is too deep. Reducing it to MAX_OCTREE_DEPTH" << std::endl;
        this->push_constants.max_octree_depth = MAX_OCTREE_DEPTH;
    }

    std::cout << "[MeshShaderRenderer::init] sizeof (PushConstantsData): " << sizeof (PushConstantsData) << std::endl;

    const int tasks_count = this->subtrees.size ();

    std::cout << "[MeshShaderRenderer::init] subtrees (tasks) count: " << tasks_count << std::endl;
    std::cout << "[MeshShaderRenderer::init] active leafs byte size (MAX, user-input): " << a_leaf_memory_limit << std::endl;

    auto make_divisable = [] (size_t value, size_t by) -> size_t {
        return (value / by) * by;
    };
    a_leaf_memory_limit = make_divisable (a_leaf_memory_limit, sizeof (NodeContext) * this->subtrees.size () * MESH_WORKGROUP_SIZE);
    std::cout << "[MeshShaderRenderer::init] active leafs byte size (actual, total): " << a_leaf_memory_limit << std::endl;

    this->active_leafs_size = a_leaf_memory_limit / this->subtrees.size ();
    std::cout << "[MeshShaderRenderer::init] active leafs byte size (per subtree task): " << this->active_leafs_size << std::endl;
    this->push_constants.max_leaf_count_per_task = this->active_leafs_size / sizeof (NodeContext);
    std::cout << "[MeshShaderRenderer::init] active leafs count (per subtree task): " << this->push_constants.max_leaf_count_per_task << std::endl;

    // NodeContext root;
    // root.node_index = 0;
    // root.voxel_size = 2.f;
    // root.min_corner = {-1.0f, -1.0f, -1.0f};
    // cpu_sandbox::task_generator (this->subtrees [0], this->sdf_octree.nodes);
    // cpu_sandbox::dump_obj ("new.obj");
    // exit (0);

    // dump_octree_subtree_pretty (this->sdf_octree, this->subtrees [0].node_index, 20, "", 0);
    std::cout << "---" << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].min_corner.x=" << this->subtrees [0].min_corner.x << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].min_corner.y=" << this->subtrees [0].min_corner.y << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].min_corner.z=" << this->subtrees [0].min_corner.z << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].min_corner.voxel_size=" << this->subtrees [0].voxel_size << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].min_corner.node_index=" << this->subtrees [0].node_index << std::endl;
    std::cout << "[MeshShaderRenderer::init] Subtree [0].cube_index=" << this->subtrees [0].cube_index << std::endl;
    std::cout << "---" << std::endl;

    this->init_mesh_shading_pipeline ();
    this->initialized = true;
    std::cout << "MeshShaderRenderer initialized successfully." << std::endl;
}

void MeshShaderRenderer::init_mesh_shading_pipeline () {
    std::cout << "MeshShaderRenderer::init_mesh_shading_pipeline called." << std::endl;

    const size_t shaders_count = 3;
    std::vector <VkShaderModule> shader_modules (shaders_count);
    std::vector <VkPipelineShaderStageCreateInfo> shader_stages (shaders_count);

    shader_stages [0] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/task_generator.slang.spv"
            , VK_SHADER_STAGE_TASK_BIT_EXT
            , shader_modules);

    shader_stages [1] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/mesh_sphere.slang.spv"
            , VK_SHADER_STAGE_MESH_BIT_EXT
            , shader_modules);

    shader_stages [2] = vk_utils::loadShader (this->context->get_device ()
            , "./assets/shaders/simple_color.slang.spv"
            , VK_SHADER_STAGE_FRAGMENT_BIT
            , shader_modules);

    VkPushConstantRange pushConstantRange {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = sizeof (PushConstantsData);
    pushConstantRange.offset = 0;

    vk_utils::DescriptorTypesVec ds_type_vec {};
    ds_type_vec.emplace_back (VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000);
    this->descriptor_maker = std::make_shared <vk_utils::DescriptorMaker> (
            this->context->get_device ()
            , ds_type_vec
            , 10
            );
	this->sdf_octree_ds = create_sdf_octree_descriptor_set (this->context->get_device ()
			, this->context->get_physical_device ()
			, this->sdf_octree
			, this->subtrees
			, this->context->get_copy_helper ()
			, *descriptor_maker
			, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT
			, this->active_leafs_size
			, this->context->get_total_frames ());

	this->marching_cubes_lookup_table_ds = create_lookup_table_descriptor_set (this->context->get_device ()
			, this->context->get_physical_device ()
			, this->context->get_copy_helper ()
			, *descriptor_maker
			, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT);

    std::vector <VkDescriptorSetLayout> descriptor_set_layouts {};
    descriptor_set_layouts.push_back (this->sdf_octree_ds.descriptor_set_layout);
    descriptor_set_layouts.push_back (this->marching_cubes_lookup_table_ds.descriptor_set_layout);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = descriptor_set_layouts.size ();
    pipelineLayoutInfo.pSetLayouts = descriptor_set_layouts.data ();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VK_CHECK_RESULT (vkCreatePipelineLayout (this->context->get_device (), &pipelineLayoutInfo, nullptr, &this->pipeline_layout));

    VkPipelineVertexInputStateCreateInfo vertexInputInfo {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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

    pipelineInfo.layout = pipeline_layout;
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
                , &this->pipeline));

    for (VkShaderModule module : shader_modules) {
        if (module != VK_NULL_HANDLE) {
            vkDestroyShaderModule (this->context->get_device (), module, nullptr);
        }
    }
    shader_modules.clear ();
}

void MeshShaderRenderer::render (const Camera& a_camera) {
    if (!this->initialized) {
        std::cerr << "Warning: MeshShaderRenderer::render called before init()." << std::endl;
        return;
    }

    vkDeviceWaitIdle (this->context->get_device ());

    auto cmd_buff = this->context->begin_frame ();
    if (cmd_buff == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline (cmd_buff, VK_PIPELINE_BIND_POINT_GRAPHICS, this->pipeline);

    vkCmdBindDescriptorSets (
            cmd_buff,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            this->pipeline_layout,
            0,
            1,
            &this->sdf_octree_ds.descriptor_sets [this->context->get_current_frame ()],
            0,
            nullptr
            );

    vkCmdBindDescriptorSets (
            cmd_buff,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            this->pipeline_layout,
            1,
            1,
            &this->marching_cubes_lookup_table_ds.descriptor_set,
            0,
            nullptr
            );

    this->update_push_constants (a_camera);
    vkCmdPushConstants (cmd_buff
            , pipeline_layout
            , VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT
            , 0
            , sizeof (PushConstantsData)
            , &this->push_constants
            );

    vkCmdDrawMeshTasksEXT (cmd_buff, static_cast <uint32_t> (this->subtrees.size ()), 1, 1);

    this->context->end_frame (cmd_buff);
}

void MeshShaderRenderer::resize (int a_width, int a_height) {
    if (!this->initialized) {
        std::cerr << "Warning: MeshShaderRenderer::resize called before init()." << std::endl;
        this->width = a_width;
        this->height = a_height;
        return;
    }

    std::cout << "MeshShaderRenderer resizing to " << a_width << "x" << a_height << "..." << std::endl;

    this->width = a_width;
    this->height = a_height;
}

void MeshShaderRenderer::shutdown () {
    vkDeviceWaitIdle (this->context->get_device ());

    if (!this->context || !this->context->is_initialized ()) {
        std::cerr << "[MeshShaderRenderer::shutdown] Warning: Vulkan context is already missing." << std::endl;
        return;
    }

    std::cout << "MeshShaderRenderer shutting down..." << std::endl;

    cleanup_sdf_octree_descriptor_set (this->context->get_device (), this->sdf_octree_ds);
    cleanup_lookup_table_descriptor_set (this->context->get_device (), this->marching_cubes_lookup_table_ds);

    this->descriptor_maker.reset ();

    if (this->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline (this->context->get_device (), this->pipeline, nullptr);
        this->pipeline = VK_NULL_HANDLE;
    }
    if (this->pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout (this->context->get_device (), this->pipeline_layout, nullptr);
        this->pipeline_layout = VK_NULL_HANDLE;
    }

    this->render_pass = VK_NULL_HANDLE;

    this->initialized = false;
}

void MeshShaderRenderer::update_push_constants (const Camera& a_camera) {
    float aspect_ratio = static_cast <float> (this->width) / static_cast <float> (this->height);
    this->push_constants.view_proj = a_camera.get_view_projection_matrix (aspect_ratio);
    this->push_constants.camera_pos = LiteMath::to_float4 (a_camera.camera_position, 1.f);
    this->push_constants.color = LiteMath::float4 (0.f, 1.f, 0.f, 1.f);

    std::vector <LiteMath::float4> planes = Camera::extract_frustum_planes (push_constants.view_proj);
    for (int i = 0; i < 6; ++i) {
        this->push_constants.frustum_planes [i] = planes [i];
    }

    uint insufficent_mem_flag = fetch_insufficent_mem_flag (this->context->get_copy_helper (), this->sdf_octree_ds);
    if (insufficent_mem_flag) {
        vkDeviceWaitIdle (this->context->get_device ());
        this->push_constants.max_octree_depth -= 1;
        std::cout << "[MeshShaderRenderer::update_push_constants]: insufficent_mem_flag : " << insufficent_mem_flag << ". Reducing octree depth from "
            << this->push_constants.max_octree_depth + 1 << " to " << this->push_constants.max_octree_depth << std::endl;
        if (this->push_constants.max_octree_depth == 0) {
            throw std::runtime_error ("[MeshShaderRenderer::update_push_constants]: bug. octree must not me 0 depth");
        }
    }
}


} // namespace sdf_raster

