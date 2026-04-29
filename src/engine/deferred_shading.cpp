// engine/deferred_shading.cpp
// deferred_shading.cpp

#include "deferred_shading.hpp"

#include <vk_utils.h> // VK_CHECK_RESULT

#include <array>
#include <utility> // pair

namespace {

VkRenderPass create_gbuffer_render_pass (VkDevice device, std::vector <VkFormat> gbuffer_formats, VkFormat depth_format) {
    std::vector <VkAttachmentDescription> attachments;

    for (auto format : gbuffer_formats) {
        VkAttachmentDescription att {
            .format = format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // NOTE: used in filter
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL // NOTE: used in filter
        };
        attachments.push_back (att);
    }

    VkAttachmentDescription depth_att {
        .format = depth_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // NOTE: used in filter
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL // NOTE: used in filter
    };
    attachments.push_back (depth_att);

    std::vector <VkAttachmentReference> color_refs;
    for (uint32_t i = 0; i < gbuffer_formats.size (); ++i) {
        color_refs.push_back ({i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    }

    VkAttachmentReference depth_ref {
        .attachment = static_cast <uint32_t> (gbuffer_formats.size ()),
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = static_cast <uint32_t> (color_refs.size ()),
        .pColorAttachments = color_refs.data (),
        .pDepthStencilAttachment = &depth_ref
    };

    VkRenderPassCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast <uint32_t> (attachments.size ()),
        .pAttachments = attachments.data (),
        .subpassCount = 1,
        .pSubpasses = &subpass
    };

    VkRenderPass rp;
    VK_CHECK_RESULT (vkCreateRenderPass (device, &ci, nullptr, &rp));
    return rp;
}

VkRenderPass create_lighting_render_pass (VkDevice device, VkFormat swapchain_format) {
    VkAttachmentDescription swap_attachment {
        .format = swapchain_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };

    VkAttachmentReference color_ref {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr, // NOTE: pInputAttachments is unused as we pass gbuffer as input through descriptors.
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };

    VkSubpassDependency gbuffer_dependency {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT
    };

    VkRenderPassCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &swap_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &gbuffer_dependency
    };

    VkRenderPass rp;
    VK_CHECK_RESULT (vkCreateRenderPass (device, &ci, nullptr, &rp));
    return rp;
}

VkRenderPass create_after_render_pass (VkDevice device, VkFormat depth_format, VkFormat swapchain_format) {
    std::array <VkAttachmentDescription, 2> attachments = {{
        {
            .format = swapchain_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_NONE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        },
        {
            .format = depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_NONE,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        }
    }};

    VkAttachmentReference color_ref {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_ref {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
        .pDepthStencilAttachment = &depth_ref
    };

    std::array<VkSubpassDependency, 2> dependencies = {{
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
        },
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        }
    }};

    VkRenderPassCreateInfo ci {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast <uint32_t> (attachments.size ()),
        .pAttachments = attachments.data (),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast <uint32_t> (dependencies.size ()),
        .pDependencies = dependencies.data ()
    };

    VkRenderPass rp;
    VK_CHECK_RESULT (vkCreateRenderPass (device, &ci, nullptr, &rp));
    return rp;
}

std::vector <vk_utils::VulkanImageMem> create_gbuffer_images (VkDevice device, VkPhysicalDevice physical_device
    , VkExtent2D extent, std::vector <VkFormat> gbuffer_formats, VkFormat depth_format) {
    std::vector <vk_utils::VulkanImageMem> color_images;
    std::vector <vk_utils::VulkanImageMem> depth_images;

    for (auto format : gbuffer_formats) {
        color_images.push_back (
            vk_utils::createImg (device, extent.width, extent.height, format
                , VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1)
        );
    }

    depth_images.push_back (
        vk_utils::createImg (device, extent.width, extent.height, depth_format
            , VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1)
    );

    vk_utils::allocateImgsBindCreateView (device, physical_device, color_images);
    vk_utils::allocateImgsBindCreateView (device, physical_device, depth_images);

    color_images.push_back (std::move (depth_images [0]));
    return color_images;
}

VkFramebuffer create_gbuffer_framebuffer (VkDevice device
    , VkRenderPass gbuffer_render_pass, VkExtent2D extent, const std::vector <vk_utils::VulkanImageMem>& gbuffer_images) {
    std::vector <VkImageView> attachments;
    for (const auto& img : gbuffer_images) {
        attachments.push_back (img.view);
    }

    VkFramebufferCreateInfo fb_ci {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = gbuffer_render_pass,
        .attachmentCount = static_cast <uint32_t> (attachments.size ()),
        .pAttachments = attachments.data (),
        .width = extent.width,
        .height = extent.height,
        .layers = 1
    };

    VkFramebuffer gbuffer_fb;
    VK_CHECK_RESULT (vkCreateFramebuffer (device, &fb_ci, nullptr, &gbuffer_fb));
    return gbuffer_fb;
}

VkFramebuffer create_lighting_framebuffer (VkDevice device
    , VkRenderPass lighting_render_pass, VkExtent2D extent, VkImageView swapchain_view) {

    VkImageView attachment = swapchain_view;

    VkFramebufferCreateInfo fb_ci {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = lighting_render_pass,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .width = extent.width,
        .height = extent.height,
        .layers = 1
    };

    VkFramebuffer lighting_fb;
    VK_CHECK_RESULT (vkCreateFramebuffer (device, &fb_ci, nullptr, &lighting_fb));
    return lighting_fb;
}

VkFramebuffer create_after_framebuffer (VkDevice device
    , VkRenderPass after_render_pass, VkExtent2D extent, VkImageView depth_view, VkImageView swapchain_view) {
    std::array attachments = {swapchain_view, depth_view};

    VkFramebufferCreateInfo fb_ci {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = after_render_pass,
        .attachmentCount = static_cast <uint32_t> (attachments.size ()),
        .pAttachments = attachments.data (),
        .width = extent.width,
        .height = extent.height,
        .layers = 1
    };

    VkFramebuffer after_fb;
    VK_CHECK_RESULT (vkCreateFramebuffer (device, &fb_ci, nullptr, &after_fb));
    return after_fb;
}

VkSampler create_gbuffer_sampler (VkDevice device, VkFilter filter) {
    return vk_utils::createSampler (device, filter, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

std::pair <VkDescriptorSet, VkDescriptorSetLayout> init_gbuffer_descriptor_set (VkSampler sampler
    , const std::vector <vk_utils::VulkanImageMem>& gbuffer_images, std::unique_ptr <vk_utils::DescriptorMaker>& desc_maker) {
    desc_maker->BindBegin (VK_SHADER_STAGE_FRAGMENT_BIT);

    for (size_t loc = 0; loc < gbuffer_images.size (); ++loc) {
        VkImageLayout layout = (loc == gbuffer_images.size () - 1)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        desc_maker->BindImage (loc, gbuffer_images [loc].view, sampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, layout);
    }

    std::pair <VkDescriptorSet, VkDescriptorSetLayout> result;
    desc_maker->BindEnd (&result.first, &result.second);
    return result;
}

void warmup_gbuffer_images (VkDevice device, VkCommandPool command_pool, VkQueue queue, std::vector <vk_utils::VulkanImageMem>& gbuffer_images) {
    VkCommandBuffer cmd = vk_utils::createCommandBuffer (device, command_pool);

    VkCommandBufferBeginInfo begin_info {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer (cmd, &begin_info);

    for (auto& img : gbuffer_images) {
        VkImageLayout target_layout = (img.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL 
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = target_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img.image,
            .subresourceRange = VkImageSubresourceRange {
                .aspectMask = img.aspectMask,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        vkCmdPipelineBarrier (cmd
            , VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            , 0
            , 0, nullptr
            , 0, nullptr
            , 1, &barrier);
    }

    vkEndCommandBuffer (cmd);

    vk_utils::executeCommandBufferNow (cmd, queue, device);
}

}

DeferredShading::DeferredShading (VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue
    , const DeferredShadingConfig& config, std::shared_ptr <sdf_raster::PresentationContext> a_presentation) : device (device)
    , presentation_context (std::move (a_presentation)) {
    VkExtent2D extent = this->presentation_context->get_extent ();
    VkFormat depth_format;
    if (!vk_utils::getSupportedDepthFormat (physical_device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}, &depth_format)) {
        throw std::runtime_error ("couldn't find supported depth format");
    }

    this->gbuffer_pass = create_gbuffer_render_pass (device, config.gbuffer_formats, depth_format);
    this->lighting_pass = create_lighting_render_pass (device, this->presentation_context->get_swapchain_image_format ());
    this->after_pass = create_after_render_pass (device, depth_format, this->presentation_context->get_swapchain_image_format ());

    this->g_buffer = create_gbuffer_images (device, physical_device, extent, config.gbuffer_formats, depth_format);
    this->g_buffer_framebuffer = create_gbuffer_framebuffer (device, this->gbuffer_pass, extent, this->g_buffer);
    warmup_gbuffer_images (device, command_pool, queue, this->g_buffer);

    uint32_t swapchain_image_count = this->presentation_context->get_swapchain_image_count ();
    auto swapchain_views = this->presentation_context->get_swapchain_image_views ();

    this->lighting_framebuffers.resize (swapchain_image_count);
    this->after_framebuffers.resize (swapchain_image_count);
    for (size_t i = 0; i < swapchain_image_count; ++i) {
        assert (this->g_buffer.back ().aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT && "last image of g_buffer must be depth buffer");
        this->lighting_framebuffers [i] = create_lighting_framebuffer (device, this->lighting_pass, extent, swapchain_views [i]);
        this->after_framebuffers [i] = create_after_framebuffer (device, this->after_pass, extent, this->g_buffer.back ().view, swapchain_views [i]);
    }

    this->sampler = create_gbuffer_sampler (device, config.filter);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, config.gbuffer_formats.size () + 1 }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, 1);
    auto desc_res = init_gbuffer_descriptor_set (this->sampler, this->g_buffer, this->desc_maker);
    this->descriptor_set = desc_res.first;
    this->descriptor_set_layout = desc_res.second;
}

DeferredShading::~DeferredShading () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    if (this->g_buffer_framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer (this->device, this->g_buffer_framebuffer, nullptr);
    }

    if (!this->g_buffer.empty () && this->g_buffer [0].mem != VK_NULL_HANDLE) {
        // NOTE: this->g_buffer [0] is enough, as we allocated one memory for all colored image.
        vkFreeMemory (device, this->g_buffer [0].mem, nullptr);
    }

    assert (this->g_buffer.back ().aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT && "last image of g_buffer must be depth buffer");
    if (!this->g_buffer.empty () && this->g_buffer.back ().mem != VK_NULL_HANDLE) {
        // NOTE: depth was allocated sepparately
        vkFreeMemory (device, this->g_buffer.back ().mem, nullptr);
    }

    for (auto& img : this->g_buffer) {
        img.mem = VK_NULL_HANDLE; // NOTE: free'd manualy
        vk_utils::deleteImg (device, &img);
    }

    for (auto fb : this->lighting_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->device, fb, nullptr);
        }
    }
    this->lighting_framebuffers.clear ();

    for (auto fb : this->after_framebuffers) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->device, fb, nullptr);
        }
    }
    this->after_framebuffers.clear ();

    if (this->sampler != VK_NULL_HANDLE) vkDestroySampler (this->device, this->sampler, nullptr);
    if (this->gbuffer_pass != VK_NULL_HANDLE) vkDestroyRenderPass (this->device, this->gbuffer_pass, nullptr);
    if (this->lighting_pass != VK_NULL_HANDLE) vkDestroyRenderPass (this->device, this->lighting_pass, nullptr);
    if (this->after_pass != VK_NULL_HANDLE) vkDestroyRenderPass (this->device, this->after_pass, nullptr);

    this->sampler = VK_NULL_HANDLE;
    this->gbuffer_pass = VK_NULL_HANDLE;
    this->lighting_pass = VK_NULL_HANDLE;
    this->after_pass = VK_NULL_HANDLE;
    this->device = VK_NULL_HANDLE;
}

DeferredShading::DeferredShading (DeferredShading&& other) noexcept {
    *this = std::move (other);
}

DeferredShading& DeferredShading::operator= (DeferredShading&& other) noexcept {
    if (this != &other) {
        this->~DeferredShading ();

        this->device = other.device;
        this->gbuffer_pass = other.gbuffer_pass;
        this->lighting_pass = other.lighting_pass;
        this->after_pass = other.after_pass;
        this->sampler = other.sampler;
        this->descriptor_set = other.descriptor_set;
        this->descriptor_set_layout = other.descriptor_set_layout;

        this->g_buffer = std::move (other.g_buffer);
        this->lighting_framebuffers = std::move (other.lighting_framebuffers);
        this->after_framebuffers = std::move (other.after_framebuffers);
        this->desc_maker = std::move (other.desc_maker);

        other.device = VK_NULL_HANDLE;
        other.gbuffer_pass = VK_NULL_HANDLE;
        other.lighting_pass = VK_NULL_HANDLE;
        other.after_pass = VK_NULL_HANDLE;
        other.sampler = VK_NULL_HANDLE;
        other.descriptor_set_layout = VK_NULL_HANDLE;
    }
    return *this;
}


