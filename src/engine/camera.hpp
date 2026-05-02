// engine/camera.hpp
// camera.hpp

#pragma once

#include <LiteMath.h>
#include <nlohmann/json.hpp> // nlohmann::adl_serializer
#include <vk_copy.h>

#include "utils/quaternion.hpp"

#include <array>
#include <cmath>
#include <memory>

namespace sdf_raster {

class Camera;
namespace gui {
    void camera_window (Camera& camera);
}

class Camera {
public:
    Camera ();

    const LiteMath::float3& get_position () const;
    const LiteMath::float4x4& get_view_projection_matrix () const;
    const LiteMath::float4x4& get_view_matrix () const;
    const LiteMath::float4x4& get_projection_matrix () const;
    const std::array <LiteMath::float4, 8>& get_frustum_corners () const;

    float get_near_plane () const;
    float get_far_plane () const;
    float get_fov_y () const;

    void set_aspect_ratio (float aspect_ratio);
    void set_far_plane (float far_plane);

    void move (LiteMath::float3 direction, float delta_time);
    void rotate (float x_offset, float y_offset);
    void adjust_fov (float offset);

    void reset ();
    void update ();

    friend void gui::camera_window (Camera& camera);
    friend struct nlohmann::adl_serializer <Camera>;

private:
    // TODO: create static camera instance with default values
    LiteMath::float3 default_position {1.f};
    math::Quat default_orientation {0.f, 0.f, 0.f, 1.f};
    float default_fov_y {45.5f};

    LiteMath::float3 position {default_position};
    math::Quat orientation {default_orientation};

    LiteMath::float3 front {};
    LiteMath::float3 right {};
    LiteMath::float3 up {};

    LiteMath::float4x4 view_matrix;
    LiteMath::float4x4 projection_matrix;
    LiteMath::float4x4 view_projection_matrix;
    LiteMath::float4x4 inv_view_projection_matrix;

    float movement_speed {0.25f};
    float mouse_sensitivity {0.1f};

    float fov_y {default_fov_y};
    float near_plane {0.01f};
    float far_plane {10.0f};
    float aspect_ratio {1.0f};

    std::array <LiteMath::float4, 8> frustum_corners;
};

class FrustumDrawBuffer {
public:
    static std::unique_ptr <FrustumDrawBuffer> get_frustum_buffer (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , Camera& camera);

    inline Camera get_camera () { return this->saved_camera; };
    inline VkBuffer get_vertex_buffer () { return this->vertex_buffer; };
    inline VkBuffer get_index_buffer () { return this->index_buffer; };
    inline size_t get_index_count () { return 24; };

    ~FrustumDrawBuffer ();

private:
    FrustumDrawBuffer (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , Camera& camera);

    VkDevice deletion_device;
    Camera saved_camera;

    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

} // sdf_raster

