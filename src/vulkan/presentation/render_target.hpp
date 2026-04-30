// vulkan/presentation/render_target.hpp
#pragma once

#include "../context/vulkan_context.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace sdf_raster {

// NOTE:
// - get_image_count() / get_image_views() describe output attachment images/views
//   available for framebuffer construction.
// - They do NOT describe frames-in-flight.
// - PresentationRenderTarget returns swapchain-backed output views.
// - OffscreenRenderTarget returns a vector of size 1 for compatibility with
//   code that builds one framebuffer per output view.
class RenderTarget {
public:
    virtual ~RenderTarget () = default;

    virtual void shutdown () = 0;
    virtual bool is_initialized () const = 0;

    virtual VkExtent2D get_extent () const = 0;
    virtual VkFormat get_image_format () const = 0;
    virtual VkImageLayout get_output_final_layout () const = 0;

    virtual uint32_t get_image_count () const = 0;
    virtual std::vector <VkImageView> get_image_views () = 0;

    virtual uint32_t get_max_frames_in_flight () const = 0;

    virtual VkCommandBuffer begin_frame (uint32_t frame_idx) = 0;
    virtual void end_frame (VkCommandBuffer cmd_buff, uint32_t frame_idx) = 0;

    virtual void register_resizable (std::function <void ()> callback) = 0;
    virtual void set_resized_flag () {}

    virtual uint32_t get_current_image_index () const { return 0; }

protected:
    std::shared_ptr <VulkanContext> context = nullptr;
};

} // namespace sdf_raster
