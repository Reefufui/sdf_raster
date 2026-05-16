// engine/camera.cpp
#include "camera.hpp"

#include "shader_common.hpp"
#include "logger.hpp"

#include <vk_buffers.h>
#include <vk_utils.h>

#include <fstream>
#include <vector>

namespace sdf_raster {

Camera::Camera () {
}

const LiteMath::float3& Camera::get_position () const {
    return this->position;
}

const LiteMath::float4x4& Camera::get_view_projection_matrix () const {
    return this->view_projection_matrix;
}

const LiteMath::float4x4& Camera::get_view_matrix () const {
    return this->view_matrix;
}

const LiteMath::float4x4& Camera::get_projection_matrix () const {
    return this->projection_matrix;
}

const std::array <LiteMath::float4, 8>& Camera::get_frustum_corners () const {
    return this->frustum_corners;
}

float Camera::get_near_plane () const {
    return this->near_plane;
}

float Camera::get_far_plane () const {
    return this->far_plane;
}

float Camera::get_fov_y () const {
    return this->fov_y;
}

void Camera::set_aspect_ratio (float aspect_ratio) {
    this->aspect_ratio = aspect_ratio;
}

void Camera::set_far_plane (float far_plane) {
    this->far_plane = far_plane;
}

void Camera::move (LiteMath::float3 direction, float delta_time) {
    LiteMath::float3 delta_distance = direction * this->movement_speed * delta_time;
    this->position += this->front * delta_distance.x;
    this->position += this->right * delta_distance.y;
    this->position += this->up * delta_distance.z;
}

void Camera::rotate (float x_offset, float y_offset) {
    x_offset *= this->mouse_sensitivity;
    y_offset *= this->mouse_sensitivity;

    // Yaw around world up (0, 1, 0)
    math::Quat q_yaw = math::quat_from_axis_angle (LiteMath::float3 (0.0f, 1.0f, 0.0f), -x_offset);
    // Pitch around local right (1, 0, 0)
    math::Quat q_pitch = math::quat_from_axis_angle (LiteMath::float3 (1.0f, 0.0f, 0.0f), -y_offset);

    this->orientation = math::quat_mul (q_yaw, math::quat_mul (this->orientation, q_pitch));
    this->orientation = LiteMath::normalize (this->orientation);
}

void Camera::adjust_fov (float offset) {
    this->fov_y -= offset;
    if (this->fov_y < 1.0f) this->fov_y = 1.0f;
    if (this->fov_y > 120.0f) this->fov_y = 120.0f;
}

void Camera::update () {
    LiteMath::float4x4 rot_mat = math::quat_to_mat4 (this->orientation);

    // Extract basis vectors from rotation matrix columns
    this->up    = LiteMath::normalize (LiteMath::float3 (rot_mat.col (1).x, rot_mat.col (1).y, rot_mat.col (1).z));
    this->front = LiteMath::normalize (LiteMath::float3 (rot_mat.col (2).x, rot_mat.col (2).y, rot_mat.col (2).z));
    this->right = LiteMath::cross (this->front, this->up);

    this->view_matrix = LiteMath::lookAt (this->position, this->position + this->front, this->up);

    this->projection_matrix = LiteMath::perspectiveMatrix (this->fov_y, this->aspect_ratio, this->near_plane, this->far_plane);

    // Vulkan NDC: Y is down, Z is [0, 1]
    this->projection_matrix (1, 1) *= -1.0f;

    this->view_projection_matrix = this->projection_matrix * this->view_matrix;
    this->inv_view_projection_matrix = LiteMath::inverse4x4 (this->view_projection_matrix);

    static LiteMath::float4 clip_corners [8] = {
        LiteMath::float4 (-1, -1, 0, 1), LiteMath::float4 (1, -1, 0, 1), // BL, BR Near
        LiteMath::float4 (-1,  1, 0, 1), LiteMath::float4 (1,  1, 0, 1), // TL, TR Near
        LiteMath::float4 (-1, -1, 1, 1), LiteMath::float4 (1, -1, 1, 1), // BL, BR Far
        LiteMath::float4 (-1,  1, 1, 1), LiteMath::float4 (1,  1, 1, 1)  // TL, TR Far
    };

    for (size_t i = 0; i < 8; ++i) {
        LiteMath::float4 world_p = this->inv_view_projection_matrix * clip_corners [i];
        world_p /= world_p.w;
        this->frustum_corners [i] = LiteMath::float4 (world_p.x, world_p.y, world_p.z, 1.f);
    }
}

void Camera::update (float delta_time) {
    if (this->control_mode == ControlMode::Trajectory && this->active_trajectory_idx >= 0) {
        const auto& trajectory = this->trajectories [this->active_trajectory_idx];
        this->trajectory_elapsed_time += delta_time;

        float normalized_time = this->trajectory_elapsed_time / trajectory.duration;
        if (normalized_time >= 1.0f) {
            normalized_time = 1.0f;
            this->control_mode = ControlMode::Static;
        }

        Keyframe k = this->interpolate (trajectory, normalized_time);
        this->position = k.position;
        this->orientation = k.orientation;
    }

    this->update ();
}

void Camera::set_control_mode (ControlMode mode) {
    this->control_mode = mode;
}

Camera::ControlMode Camera::get_control_mode () const {
    return this->control_mode;
}

std::vector <Trajectory>& Camera::get_trajectories () {
    return this->trajectories;
}

void Camera::play_trajectory (int index) {
    if (index >= 0 && index < static_cast <int> (this->trajectories.size ())) {
        this->active_trajectory_idx = index;
        this->trajectory_elapsed_time = 0.0f;
        this->control_mode = ControlMode::Trajectory;
    }
}

Keyframe Camera::interpolate (const Trajectory& trajectory, float normalized_time) const {
    if (trajectory.keyframes.empty ()) {
        return { this->position, this->orientation };
    }
    if (trajectory.keyframes.size () == 1) {
        return trajectory.keyframes [0];
    }

    std::vector <float> segment_lengths;
    float total_length = 0.0f;
    for (size_t i = 0; i < trajectory.keyframes.size () - 1; ++i) {
        float len = LiteMath::length (trajectory.keyframes [i + 1].position - trajectory.keyframes [i].position);
        segment_lengths.push_back (len);
        total_length += len;
    }

    if (total_length < 1e-6f) {
        float segment_t = normalized_time * static_cast <float> (trajectory.keyframes.size () - 1);
        size_t idx = static_cast <size_t> (std::floor (segment_t));
        idx = std::min (idx, trajectory.keyframes.size () - 2);
        float t = segment_t - static_cast <float> (idx);

        const auto& k1 = trajectory.keyframes [idx];
        const auto& k2 = trajectory.keyframes [idx + 1];

        return {
            LiteMath::lerp (k1.position, k2.position, t)
            , math::slerp (k1.orientation, k2.orientation, t)
        };
    }

    float target_dist = normalized_time * total_length;
    float current_dist = 0.0f;
    size_t segment_idx = 0;

    for (size_t i = 0; i < segment_lengths.size (); ++i) {
        if (current_dist + segment_lengths [i] >= target_dist) {
            segment_idx = i;
            break;
        }
        current_dist += segment_lengths [i];
        segment_idx = i;
    }

    float t = (target_dist - current_dist) / std::max (segment_lengths [segment_idx], 1e-6f);
    t = std::clamp (t, 0.0f, 1.0f);

    const auto& k1 = trajectory.keyframes [segment_idx];
    const auto& k2 = trajectory.keyframes [segment_idx + 1];

    return {
        LiteMath::lerp (k1.position, k2.position, t)
        , math::slerp (k1.orientation, k2.orientation, t)
    };
}

void Camera::reset () {
    this->position = this->default_position;
    this->orientation = this->default_orientation;
    this->fov_y = this->default_fov_y;
    this->movement_speed = 0.25f;
    this->mouse_sensitivity = 0.1f;
}

namespace {

static const std::array <uint32_t, 24> s_frustum_edge_indices = {
    // Near Plane
    0, 1,
    1, 3,
    3, 2,
    2, 0,

    // Far Plane
    4, 5,
    5, 7,
    7, 6,
    6, 4,

    // Connecting Edges
    0, 4,
    1, 5,
    2, 6,
    3, 7
};

}

std::unique_ptr <FrustumDrawBuffer> FrustumDrawBuffer::get_frustum_buffer (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , Camera& camera) {
    return std::unique_ptr <FrustumDrawBuffer> (new FrustumDrawBuffer (device, physical_device, copy_helper, camera));
}

FrustumDrawBuffer::FrustumDrawBuffer (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , Camera& camera) : deletion_device (device), saved_camera (camera) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    const auto& vertices = camera.get_frustum_corners ();
    const VkDeviceSize vertices_size = vertices.size () * sizeof (vertices [0]);
    const VkDeviceSize indices_size = s_frustum_edge_indices.size () * sizeof (s_frustum_edge_indices [0]);

    this->vertex_buffer = vk_utils::createBuffer (device, vertices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT, nullptr);
    this->index_buffer = vk_utils::createBuffer (device, indices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT, nullptr);
    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, { this->vertex_buffer, this->index_buffer });

    copy_helper->UpdateBuffer (this->vertex_buffer, 0, vertices.data (), vertices_size);
    copy_helper->UpdateBuffer (this->index_buffer, 0, s_frustum_edge_indices.data (), indices_size);

    camera.set_far_plane (100.f); // to render frustum fully
}

FrustumDrawBuffer::~FrustumDrawBuffer () {
    vkDeviceWaitIdle (this->deletion_device);

    if (this->vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->deletion_device, this->vertex_buffer, nullptr);
        this->vertex_buffer = VK_NULL_HANDLE;
    }
    if (this->index_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (this->deletion_device, this->index_buffer, nullptr);
        this->index_buffer = VK_NULL_HANDLE;
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (this->deletion_device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

}

