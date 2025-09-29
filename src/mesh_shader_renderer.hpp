#pragma once

#include <memory>
#include <vector>
#include <string>
#include <iostream>

#include "camera.hpp"
#include "mesh.hpp"
#include "renderer.hpp"
#include "vk_descriptor_sets.h"
#include "vulkan_context.hpp"
#include "vulkan_context.hpp"

namespace sdf_raster {

class MeshShaderRenderer : public IRenderer {
public:
    explicit MeshShaderRenderer (std::shared_ptr <VulkanContext> vulkan_context);
    ~MeshShaderRenderer () override;

    void init (int a_width, int a_height) override;
    void render (const Camera& a_camera) override;
    void resize (int a_width, int a_height) override;
    void shutdown () override;

private:
    void init_mesh_shading_pipeline ();

    std::shared_ptr <VulkanContext> context {nullptr};
    std::shared_ptr <vk_utils::DescriptorMaker> descriptor_maker {nullptr};

    VkRenderPass render_pass {VK_NULL_HANDLE};
    VkPipelineLayout pipeline_layout {VK_NULL_HANDLE};
    VkPipeline pipeline {VK_NULL_HANDLE};

    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorPool descriptor_pool {VK_NULL_HANDLE};
    VkDescriptorSetLayout descriptor_set_layout {VK_NULL_HANDLE};

    int width {};
    int height {};

    struct PushConstantsData {
        int x;
    };

    bool initialized {false};
};

} // namespace sdf_raster

