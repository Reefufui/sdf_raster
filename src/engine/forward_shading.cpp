// engine/forward_shading.cpp

#include "forward_shading.hpp"

#include <vk_utils.h>

#include <array>
#include <stdexcept>
#include <utility>

namespace sdf_raster {

ForwardShading::ForwardShading (VkDevice a_device, VkPhysicalDevice a_physical_device, std::shared_ptr <RenderTarget> a_render_target, const std::unique_ptr <vk_utils::VulkanImageMem>& a_depth_buffer)
    : render_target (std::move (a_render_target))
    , device (a_device)
    , depth_buffer (a_depth_buffer) {

    if (!vk_utils::getSupportedDepthFormat (a_physical_device, {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}, &this->depth_format)) {
        throw std::runtime_error ("couldn't find supported depth format");
    }

    this->create_render_passes ();
    this->create_framebuffers ();
}

ForwardShading::~ForwardShading () {
    if (this->device == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle (this->device);

    this->destroy_framebuffers ();
    this->destroy_render_passes ();

    this->device = VK_NULL_HANDLE;
}

void ForwardShading::create_render_passes () {
    this->main.render_pass = this->create_render_pass (VK_ATTACHMENT_LOAD_OP_CLEAR);
    this->after.render_pass = this->create_render_pass (VK_ATTACHMENT_LOAD_OP_NONE);
}

VkRenderPass ForwardShading::create_render_pass (VkAttachmentLoadOp load_op) {
    VkAttachmentDescription color_attachment {
        .format = this->render_target->get_image_format (),
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = load_op,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = this->render_target->get_output_final_layout ()
    };

    VkAttachmentReference color_attachment_ref {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    assert (this->depth_format != VK_FORMAT_UNDEFINED);

    VkAttachmentDescription depth_attachment {
        .format = this->depth_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = load_op,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_attachment_ref {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_ref,
        .pDepthStencilAttachment = &depth_attachment_ref
    };

    std::array <VkSubpassDependency, 2> dependencies;

    dependencies [0] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
    };

    dependencies [1] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };

    std::array attachments = {color_attachment, depth_attachment};

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
    VK_CHECK_RESULT (vkCreateRenderPass (this->device, &ci, nullptr, &rp));
    return rp;
}

void ForwardShading::create_framebuffers () {
    std::array <VkImageView, 2> attachments = {VK_NULL_HANDLE, this->depth_buffer->view};

    const auto extent = this->render_target->get_extent ();
    const uint32_t image_count = this->render_target->get_image_count ();

    VkFramebufferCreateInfo fb_ci {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .attachmentCount = static_cast <uint32_t> (attachments.size ()),
        .pAttachments = attachments.data (),
        .width = extent.width,
        .height = extent.height,
        .layers = 1,
    };

    this->main.framebuffer.resize (image_count);
    this->after.framebuffer.resize (image_count);

    auto output_views = this->render_target->get_image_views ();
    for (uint32_t i = 0; i < image_count; i++) {
        attachments [0] = output_views [i];

        fb_ci.renderPass = this->main.render_pass;
        VK_CHECK_RESULT (vkCreateFramebuffer (this->device, &fb_ci, nullptr, &this->main.framebuffer [i]));

        fb_ci.renderPass = this->after.render_pass;
        VK_CHECK_RESULT (vkCreateFramebuffer (this->device, &fb_ci, nullptr, &this->after.framebuffer [i]));
    }
}

void ForwardShading::recreate_framebuffers (VkPhysicalDevice a_physical_device) {
    this->destroy_framebuffers ();
    this->create_framebuffers ();
}

void ForwardShading::destroy_render_passes () {
    if (this->main.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass (this->device, this->main.render_pass, nullptr);
        this->main.render_pass = VK_NULL_HANDLE;
    }
    if (this->after.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass (this->device, this->after.render_pass, nullptr);
        this->after.render_pass = VK_NULL_HANDLE;
    }
}

void ForwardShading::destroy_framebuffers () {
    for (auto fb : this->main.framebuffer) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->device, fb, nullptr);
        }
    }
    this->main.framebuffer.clear ();

    for (auto fb : this->after.framebuffer) {
        if (fb != VK_NULL_HANDLE) {
            vkDestroyFramebuffer (this->device, fb, nullptr);
        }
    }
    this->after.framebuffer.clear ();
}

} // namespace sdf_raster
