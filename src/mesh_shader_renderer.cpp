#include <stdexcept>
#include <array>

#include "mesh_shader_renderer.hpp"
#include "application.hpp"

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

void MeshShaderRenderer::init (int a_width, int a_height, SdfOctree&& a_sdf_octree) {
    std::cout << "MeshShaderRenderer initializing..." << std::endl;

    if (!this->context || !this->context->is_initialized ()) {
        throw std::runtime_error("[MeshShaderRenderer::init] VulkanContext is not initialized before renderer init.");
    }

    this->width = a_width;
    this->height = a_height;
    this->sdf_octree = std::move (a_sdf_octree);

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
            , 3
            );
	this->sdf_octree_ds = create_sdf_octree_descriptor_set (this->context->get_device ()
			, this->context->get_physical_device ()
			, this->sdf_octree
			, this->context->get_copy_helper ()
			, *descriptor_maker
			, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT);

	this->marching_cubes_lookup_table_ds = create_lookup_table_descriptor_set (this->context->get_device ()
			, this->context->get_physical_device ()
			, this->context->get_copy_helper ()
			, *descriptor_maker
			, VK_SHADER_STAGE_MESH_BIT_EXT);

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

    this->update_push_constants (a_camera);

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
            &this->sdf_octree_ds.descriptor_set,
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

    vkCmdPushConstants (cmd_buff
            , pipeline_layout
            , VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT
            , 0
            , sizeof (PushConstantsData)
            , &this->push_constants
            );

    vkCmdDrawMeshTasksEXT (cmd_buff, 1, 1, 1);

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
    this->push_constants.camera_pos = a_camera.camera_position;
    this->push_constants.padding = 1.0f;
    this->push_constants.color = LiteMath::float4 (0.f, 1.f, 0.f, 1.f);

    std::vector <LiteMath::float4> planes = Camera::extract_frustum_planes (push_constants.view_proj);
    for (int i = 0; i < 6; ++i) {
        this->push_constants.frustum_planes [i] = planes [i];
    }
}


} // namespace sdf_raster

