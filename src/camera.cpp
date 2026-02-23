#include <fstream>
#include <vector>

#include "camera.hpp"
#include "logger.hpp"
#include "nlohmann/json.hpp"
#include "shaders/common.h"
#include "vk_buffers.h"
#include "vk_utils.h"

namespace sdf_raster {

Camera::Camera () {
}

const LiteMath::float3& Camera::get_position () const {
    return this->camera_position;
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

void Camera::set_aspect_ratio (float aspect_ratio) {
    this->aspect_ratio = aspect_ratio;
}

void Camera::set_far_plane (float far_plane) {
    this->far_plane = far_plane;
}

void Camera::process_keyboard_input (Camera::Movement direction, float delta_time) {
    float velocity = this->movement_speed * delta_time;
    if (direction == Camera::Movement::FORWARD)
        this->camera_position += camera_front * velocity;
    if (direction == Camera::Movement::BACKWARD)
        this->camera_position -= camera_front * velocity;
    if (direction == Camera::Movement::LEFT)
        this->camera_position -= camera_right * velocity;
    if (direction == Camera::Movement::RIGHT)
        this->camera_position += camera_right * velocity;
    if (direction == Camera::Movement::UP)
        this->camera_position += LiteMath::float3 (0.0f, 1.0f, 0.0f) * velocity;
    if (direction == Camera::Movement::DOWN)
        this->camera_position -= LiteMath::float3 (0.0f, 1.0f, 0.0f) * velocity;
}

void Camera::process_mouse_movement (float x_offset, float y_offset, bool constrain_pitch) {
    x_offset *= this->mouse_sensitivity;
    y_offset *= this->mouse_sensitivity;

    this->yaw_angle -= x_offset;
    this->pitch_angle += y_offset;

    if (constrain_pitch) {
        if (this->pitch_angle > 89.9f) this->pitch_angle = 89.9f;
        if (this->pitch_angle < -89.9f) this->pitch_angle = -89.9f;
    }
}

void Camera::process_scroll (float offset, bool constrain_fov) {
    this->fov_y -= offset;
    if (constrain_fov) {
        if (this->fov_y < 1.0f) this->fov_y = 1.0f;
        if (this->fov_y > 60.0f) this->fov_y = 60.0f;
    }
}

void Camera::update () {
    LiteMath::float3 new_front;
    new_front.x = std::cos (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);
    new_front.y = std::sin (LiteMath::DEG_TO_RAD * pitch_angle);
    new_front.z = std::sin (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);

    this->camera_front = LiteMath::normalize (new_front);
    this->camera_right = LiteMath::normalize (LiteMath::cross (this->camera_front, LiteMath::float3 (0.0f, -1.0f, 0.0f)));
    this->camera_up    = LiteMath::normalize (LiteMath::cross (this->camera_right, this->camera_front));

    this->view_matrix = LiteMath::lookAt (this->camera_position, this->camera_position + this->camera_front, this->camera_up);
    this->projection_matrix = LiteMath::perspectiveMatrix (this->fov_y, this->aspect_ratio, this->near_plane, this->far_plane);
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

inline nlohmann::json float3_to_json (const LiteMath::float3& vec) {
    return {{"x", vec.x}, {"y", vec.y}, {"z", vec.z}};
}

inline LiteMath::float3 json_to_float3 (const nlohmann::json& j) {
    return LiteMath::float3 (j.at ("x").get <float> (), j.at ("y").get <float> (), j.at ("z").get <float> ());
}

void Camera::dump (const std::string& filename) const {
    nlohmann::json j;
    j ["camera_position"] = float3_to_json (camera_position);
    j ["yaw_angle"] = yaw_angle;
    j ["pitch_angle"] = pitch_angle;
    j ["fov_y"] = fov_y;
    j ["movement_speed"] = movement_speed;
    j ["mouse_sensitivity"] = mouse_sensitivity;

    std::ofstream o (filename);
    if (o.is_open ()) {
        o << std::setw (4) << j << std::endl;
        o.close();
        LOG_INFO ("[Camera] Cached camera settings to '{}' file for future use.", filename);
    } else {
        LOG_ERROR ("[Camera] Couldn't open file '{}' for dumping camera settings.", filename);
    }
}

void Camera::load (const std::string& filename) {
    std::ifstream i (filename);
    if (i.is_open ()) {
        try {
            nlohmann::json j;
            i >> j;

            camera_position = json_to_float3 (j.at ("camera_position"));
            yaw_angle = j.at ("yaw_angle").get <float> ();
            pitch_angle = j.at ("pitch_angle").get <float> ();
            fov_y = j.at ("fov_y").get <float> ();
            movement_speed = j.at ("movement_speed").get <float> ();
            mouse_sensitivity = j.at ("mouse_sensitivity").get <float> ();
        } catch (const nlohmann::json::exception& e) {
            LOG_ERROR ("[Camera] Failed to parse camera settings from JSON '{}': {}.", filename, e.what ());
        } catch (const std::exception& e) {
            LOG_ERROR ("[Camera] Failed to load camera settings: {}..", e.what ());
        }
        i.close ();
        LOG_INFO ("[Camera] Restored cached camera settings from '{}'.", filename);
    } else {
        LOG_WARN ("[Camera] Failed to open camera settings file '{}'. Fall back to defaults.", filename);
    }
}

void Camera::reset () {
    this->camera_position = LiteMath::float3 (2.0f, 0.5f, -1.0f);
    this->camera_up = LiteMath::float3 (0.0f, -1.0f, 0.0f);
    this->camera_front = LiteMath::float3 (0.0f, 0.0f, -1.0f);
    this->yaw_angle = -200.0f;
    this->pitch_angle = -15.0f;
    this->fov_y = 45.0f;
    this->movement_speed = 2.5f;
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

FrustumDescriptorSetInfo create_frustum_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags
    , size_t max_frames_in_flight) {
    FrustumDescriptorSetInfo info = {};

    VkDeviceSize frustum_geometry_size = sizeof (FrustumGeometry);

    info.frustum_geometry_buffers.resize (max_frames_in_flight);
    info.frustum_geometry_memories.resize (max_frames_in_flight);
    info.frustum_geometry_memories_mapped.resize (max_frames_in_flight);

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        VkMemoryRequirements mem_req;
        info.frustum_geometry_buffers [i] = vk_utils::createBuffer (device, frustum_geometry_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &mem_req);

        VkMemoryAllocateInfo allocInfo {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = mem_req.size;
        allocInfo.memoryTypeIndex = vk_utils::findMemoryType (mem_req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, physical_device);

        VK_CHECK_RESULT (vkAllocateMemory (device, &allocInfo, nullptr, &(info.frustum_geometry_memories [i])));

        vkBindBufferMemory (device, info.frustum_geometry_buffers [i], info.frustum_geometry_memories [i], 0);

        VK_CHECK_RESULT (vkMapMemory (device, info.frustum_geometry_memories [i], 0, frustum_geometry_size, 0, &(info.frustum_geometry_memories_mapped [i])));
    }

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.frustum_geometry_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_frustum_descriptor_set (VkDevice device, FrustumDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.frustum_geometry_buffers.size (); ++i) {
        if (info.frustum_geometry_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.frustum_geometry_buffers [i], nullptr);
            info.frustum_geometry_buffers [i] = VK_NULL_HANDLE;
        }
        if (info.frustum_geometry_memories [i] != VK_NULL_HANDLE) {
            vkFreeMemory (device, info.frustum_geometry_memories [i], nullptr);
            info.frustum_geometry_memories [i] = VK_NULL_HANDLE;
        }
    }

    info = {};
}

}

