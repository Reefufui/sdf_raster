#pragma once

#include <vector>
#include <cmath>

#include "LiteMath.h"

namespace sdf_raster {

enum Camera_Movement {
    FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN
};

struct Camera {
    Camera (LiteMath::float3 initial_position = LiteMath::float3 (0.0f, 0.0f, 0.0f)
            , LiteMath::float3 initial_up = LiteMath::float3 (0.0f, -1.0f, 0.0f)
            , float initial_yaw = 0.0f
            , float initial_pitch = 0.0f);

    LiteMath::float4x4 get_view_projection_matrix (float aspect_ratio) const;

    LiteMath::float4x4 get_view_matrix () const;

    LiteMath::float4x4 get_projection_matrix (float aspect_ratio) const;

    void process_keyboard_input (Camera_Movement direction, float delta_time);

    void process_mouse_movement (float x_offset, float y_offset, bool constrain_pitch = true);

    void update_camera_vectors ();

    static std::vector<LiteMath::float4> extract_frustum_planes (const LiteMath::float4x4& view_projection_matrix);

    bool is_sphere_in_frustum (const LiteMath::float3& sphere_center , float sphere_radius , const std::vector <LiteMath::float4>& frustum_planes) const;

    void dump (const std::string& filename) const;

    void load (const std::string& filename);

    LiteMath::float3 camera_position;
    LiteMath::float3 camera_up;
    LiteMath::float3 camera_right;
    LiteMath::float3 camera_front;

    float yaw_angle;
    float pitch_angle;

    float movement_speed;
    float mouse_sensitivity;

    float fov_y;
    float near_plane;
    float far_plane;
};

}

