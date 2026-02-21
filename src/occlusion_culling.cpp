#include <iostream>

#include "occlusion_culling.hpp"

namespace sdf_raster {

DepthBufferDescriptorSetInfo create_depth_buffer_descriptor_set (
        vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const std::vector <vk_utils::VulkanImageMem>& depth_textures
        , VkSampler depth_sampler
        , size_t max_frames_in_flight) {
    std::cout << "create_depth_buffer_descriptor_set: creating..." << std::endl;
    DepthBufferDescriptorSetInfo info = {};

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindImage (0, depth_textures [i].view, depth_sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

}

