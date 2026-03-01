#pragma once

#include <array>
#include <cmath>
#include <memory>

#include "LiteMath.h"
#include "vk_copy.h"
#include "vk_descriptor_sets.h"

namespace sdf_raster {

class Camera {
public:
    Camera ();

    const LiteMath::float3& get_position () const;
    const LiteMath::float4x4& get_view_projection_matrix () const;
    const LiteMath::float4x4& get_view_matrix () const;
    const LiteMath::float4x4& get_projection_matrix () const;
    const std::array <LiteMath::float4, 8>& get_frustum_corners () const;

    void set_aspect_ratio (float aspect_ratio);
    void set_far_plane (float far_plane);

    enum class Movement {
        FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN
    };
    void process_keyboard_input (Movement direction, float delta_time);
    void process_mouse_movement (float x_offset, float y_offset, bool constrain_pitch = true);
    void process_scroll (float offset, bool constrain_fov = true);

    void dump (const std::string& filename) const;
    void load (const std::string& filename);
    void reset ();
    void update ();

private:
    LiteMath::float3 camera_position {2.0f, 0.5f, -1.0f};
    LiteMath::float3 camera_up {0.0f, -1.0f, 0.0f};
    LiteMath::float3 camera_front {0.f, 0.f, -1.f};

    LiteMath::float4x4 view_matrix;
    LiteMath::float4x4 projection_matrix;
    LiteMath::float4x4 view_projection_matrix;
    LiteMath::float4x4 inv_view_projection_matrix;

    float yaw_angle {-200.0f};
    float pitch_angle {-15.0f};

    float movement_speed {2.5f};
    float mouse_sensitivity {0.1f};

    float fov_y {45.0f};
    float near_plane {0.01f};
    float far_plane {10.0f};
    float aspect_ratio {1.0f};

    std::array <LiteMath::float4, 8> frustum_corners;

private:
    LiteMath::float3 camera_right;
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

struct FrustumDescriptorSetInfo {
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> frustum_geometry_buffers;
    std::vector <VkDeviceMemory> frustum_geometry_memories;
    std::vector <void*> frustum_geometry_memories_mapped;
};

FrustumDescriptorSetInfo create_frustum_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags
    , size_t max_frames_in_flight);

void cleanup_frustum_descriptor_set (VkDevice device, FrustumDescriptorSetInfo& info);


}

