// application/cli/offscreen_render_target.hpp
#pragma once

#include "../../engine/render_target.hpp"

#include "vk_images.h"

#include <array>
#include <memory>
#include <span>
#include <vector>

namespace sdf_raster {

class OffscreenRenderTarget : public RenderTarget {
public:
    OffscreenRenderTarget (std::shared_ptr <VulkanContext> a_context, uint32_t a_width, uint32_t a_height, VkFormat a_format);
    virtual ~OffscreenRenderTarget () override;

    VkExtent2D get_extent () const override;
    VkFormat get_image_format () const override;
    VkImageLayout get_output_final_layout () const override;

    uint32_t get_image_count () const override;
    std::vector <VkImageView> get_image_views () override;
    uint32_t get_max_frames_in_flight () const override;

    VkCommandBuffer begin_frame (uint32_t frame_idx) override;
    void end_frame (VkCommandBuffer cmd_buff, uint32_t frame_idx) override;

    std::span <const double> get_gpu_times_ns () const;
    double get_timestamp_period () const;
    void clear_gpu_times ();
    void collect_pending_timestamps ();

private:
    void create_output_image ();
    void create_frame_resources ();
    void destroy_output_image ();
    void destroy_frame_resources ();

    uint32_t find_memory_type (uint32_t type_filter, VkMemoryPropertyFlags properties);

    struct FrameResources {
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        VkQueryPool query_pool = VK_NULL_HANDLE;
        bool has_valid_timestamps = false;
        bool timestamps_consumed = true;
    };

    void collect_frame_timestamp (FrameResources& frame);

    VkExtent2D extent {};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout output_final_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    vk_utils::VulkanImageMem output_image;

    static constexpr size_t max_frames_in_flight = 2;
    std::vector <FrameResources> frame_resources;

    std::vector <double> gpu_times_ns;
    double timestamp_period = 0.0;

    std::vector <std::function <void ()>> resizable_callbacks;
};

} // namespace sdf_raster
