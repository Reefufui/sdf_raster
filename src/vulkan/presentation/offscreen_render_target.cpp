// vulkan/presentation/offscreen_render_target.cpp
#include "offscreen_render_target.hpp"
#include "logger.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace sdf_raster {

OffscreenRenderTarget::OffscreenRenderTarget (std::shared_ptr <VulkanContext> a_context, uint32_t a_width, uint32_t a_height, VkFormat a_format)
    : RenderTarget ()
    , extent { a_width, a_height }
    , format (a_format) {

    this->context = a_context;

    if (!this->context->is_initialized ()) {
        throw std::runtime_error ("[OffscreenRenderTarget] VulkanContext is not initialized.");
    }

    VkPhysicalDeviceProperties device_props {};
    vkGetPhysicalDeviceProperties (this->context->get_physical_device (), &device_props);
    this->timestamp_period = static_cast <double> (device_props.limits.timestampPeriod);

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties (this->context->get_physical_device (), &queue_family_count, nullptr);

    std::vector <VkQueueFamilyProperties> queue_props (queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties (this->context->get_physical_device (), &queue_family_count, queue_props.data ());

    bool graphics_timestamp_supported = false;
    for (const auto& props_i : queue_props) {
        if ((props_i.queueFlags & VK_QUEUE_GRAPHICS_BIT) && props_i.timestampValidBits > 0) {
            graphics_timestamp_supported = true;
            break;
        }
    }

    if (!graphics_timestamp_supported) {
        throw std::runtime_error ("[OffscreenRenderTarget] No graphics queue family with timestamp query support found.");
    }

    this->create_output_image ();
    this->create_frame_resources ();
    this->initialized = true;

    LOG_INFO ("[OffscreenRenderTarget] Created offscreen render target with extent ({}x{}) and format {}.", a_width, a_height, static_cast <int> (a_format));
}

OffscreenRenderTarget::~OffscreenRenderTarget () {
    this->shutdown ();
}

void OffscreenRenderTarget::shutdown () {
    if (!this->initialized) {
        return;
    }

    if (this->context->get_device () != VK_NULL_HANDLE) {
        vkDeviceWaitIdle (this->context->get_device ());
    }

    this->destroy_frame_resources ();
    this->destroy_output_image ();

    this->initialized = false;
    LOG_INFO ("[OffscreenRenderTarget] Shutdown complete.");
}

bool OffscreenRenderTarget::is_initialized () const {
    return this->initialized;
}

VkExtent2D OffscreenRenderTarget::get_extent () const {
    return this->extent;
}

VkFormat OffscreenRenderTarget::get_image_format () const {
    return this->format;
}

VkImageLayout OffscreenRenderTarget::get_output_final_layout () const {
    return this->output_final_layout;
}

uint32_t OffscreenRenderTarget::get_image_count () const {
    return 1;
}

std::vector <VkImageView> OffscreenRenderTarget::get_image_views () {
    return { this->output_image.view };
}

uint32_t OffscreenRenderTarget::get_max_frames_in_flight () const {
    return static_cast <uint32_t> (max_frames_in_flight);
}

void OffscreenRenderTarget::create_output_image () {
    VkImageCreateInfo image_info {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent.width = this->extent.width;
    image_info.extent.height = this->extent.height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = this->format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK_RESULT (vkCreateImage (this->context->get_device (), &image_info, nullptr, &this->output_image.image));

    VkMemoryRequirements mem_reqs {};
    vkGetImageMemoryRequirements (this->context->get_device (), this->output_image.image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;
    alloc_info.memoryTypeIndex = this->find_memory_type (mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK_RESULT (vkAllocateMemory (this->context->get_device (), &alloc_info, nullptr, &this->output_image.mem));
    VK_CHECK_RESULT (vkBindImageMemory (this->context->get_device (), this->output_image.image, this->output_image.mem, 0));

    VkImageViewCreateInfo view_info {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = this->output_image.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = this->format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VK_CHECK_RESULT (vkCreateImageView (this->context->get_device (), &view_info, nullptr, &this->output_image.view));

    this->output_image.format = this->format;
    this->output_image.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
}

void OffscreenRenderTarget::create_frame_resources () {
    this->frame_resources.resize (max_frames_in_flight);

    VkFenceCreateInfo fence_info {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkCommandBufferAllocateInfo alloc_info {};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = this->context->get_graphics_command_pool_reset ();
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkQueryPoolCreateInfo query_pool_info {};
    query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    query_pool_info.queryCount = 2;

    for (size_t i = 0; i < max_frames_in_flight; i++) {
        VK_CHECK_RESULT (vkCreateFence (this->context->get_device (), &fence_info, nullptr, &this->frame_resources [i].fence));
        VK_CHECK_RESULT (vkAllocateCommandBuffers (this->context->get_device (), &alloc_info, &this->frame_resources [i].command_buffer));
        VK_CHECK_RESULT (vkCreateQueryPool (this->context->get_device (), &query_pool_info, nullptr, &this->frame_resources [i].query_pool));
    }
}

void OffscreenRenderTarget::destroy_output_image () {
    if (this->context->get_device () == VK_NULL_HANDLE) {
        return;
    }

    if (this->output_image.view != VK_NULL_HANDLE) {
        vkDestroyImageView (this->context->get_device (), this->output_image.view, nullptr);
        this->output_image.view = VK_NULL_HANDLE;
    }

    if (this->output_image.image != VK_NULL_HANDLE) {
        vkDestroyImage (this->context->get_device (), this->output_image.image, nullptr);
        this->output_image.image = VK_NULL_HANDLE;
    }

    if (this->output_image.mem != VK_NULL_HANDLE) {
        vkFreeMemory (this->context->get_device (), this->output_image.mem, nullptr);
        this->output_image.mem = VK_NULL_HANDLE;
    }
}

void OffscreenRenderTarget::destroy_frame_resources () {
    if (this->context->get_device () == VK_NULL_HANDLE) {
        return;
    }

    for (size_t i = 0; i < max_frames_in_flight; i++) {
        if (this->frame_resources [i].fence != VK_NULL_HANDLE) {
            vkDestroyFence (this->context->get_device (), this->frame_resources [i].fence, nullptr);
            this->frame_resources [i].fence = VK_NULL_HANDLE;
        }

        if (this->frame_resources [i].query_pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool (this->context->get_device (), this->frame_resources [i].query_pool, nullptr);
            this->frame_resources [i].query_pool = VK_NULL_HANDLE;
        }
    }

    this->frame_resources.clear ();
}

uint32_t OffscreenRenderTarget::find_memory_type (uint32_t type_filter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props {};
    vkGetPhysicalDeviceMemoryProperties (this->context->get_physical_device (), &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1u << i)) && (mem_props.memoryTypes [i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error ("[OffscreenRenderTarget] Failed to find suitable memory type.");
}

VkCommandBuffer OffscreenRenderTarget::begin_frame (uint32_t frame_idx) {
    auto& frame = this->frame_resources [frame_idx];

    vkWaitForFences (this->context->get_device (), 1, &frame.fence, VK_TRUE, UINT64_MAX);

    this->collect_frame_timestamp (frame);

    vkResetFences (this->context->get_device (), 1, &frame.fence);
    vkResetCommandBuffer (frame.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK_RESULT (vkBeginCommandBuffer (frame.command_buffer, &begin_info));

    vkCmdResetQueryPool (frame.command_buffer, frame.query_pool, 0, 2);

    vkCmdWriteTimestamp (frame.command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame.query_pool, 0);

    return frame.command_buffer;
}

void OffscreenRenderTarget::end_frame (VkCommandBuffer cmd_buff, uint32_t frame_idx) {
    auto& frame = this->frame_resources [frame_idx];

    vkCmdWriteTimestamp (cmd_buff, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.query_pool, 1);

    VK_CHECK_RESULT (vkEndCommandBuffer (cmd_buff));

    VkSubmitInfo submit_info {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_buff;

    VkQueue graphics_queue = this->context->get_graphics_queue ();
    VK_CHECK_RESULT (vkQueueSubmit (graphics_queue, 1, &submit_info, frame.fence));

    frame.has_valid_timestamps = true;
    frame.timestamps_consumed = false;
}

void OffscreenRenderTarget::collect_frame_timestamp (FrameResources& frame) {
    if (!frame.has_valid_timestamps || frame.timestamps_consumed) {
        return;
    }

    std::array <uint64_t, 2> timestamps {};
    vkGetQueryPoolResults (
        this->context->get_device (),
        frame.query_pool,
        0,
        2,
        sizeof (timestamps),
        timestamps.data (),
        sizeof (uint64_t),
        VK_QUERY_RESULT_64_BIT
    );

    uint64_t delta = timestamps [1] - timestamps [0];
    double gpu_time_ns = static_cast <double> (delta) * this->timestamp_period;
    this->gpu_times_ns.push_back (gpu_time_ns);

    frame.timestamps_consumed = true;
}

void OffscreenRenderTarget::collect_pending_timestamps () {
    vkDeviceWaitIdle (this->context->get_device ());

    for (auto& frame : this->frame_resources) {
        this->collect_frame_timestamp (frame);
    }
}

std::span <const double> OffscreenRenderTarget::get_gpu_times_ns () const {
    return this->gpu_times_ns;
}

double OffscreenRenderTarget::get_timestamp_period () const {
    return this->timestamp_period;
}

void OffscreenRenderTarget::clear_gpu_times () {
    this->gpu_times_ns.clear ();
}

void OffscreenRenderTarget::register_resizable (std::function <void ()> callback) {
    this->resizable_callbacks.push_back (std::move (callback));
}

} // namespace sdf_raster
