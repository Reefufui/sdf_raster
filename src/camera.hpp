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
            , float initial_pitch = 0.0f)
        : camera_position (initial_position)
          , camera_up (initial_up)
          , camera_front (LiteMath::float3 (0.0f, 0.0f, -1.0f))
          , yaw_angle (initial_yaw)
          , pitch_angle (initial_pitch)
          , movement_speed (2.5f)
          , mouse_sensitivity (0.1f)
          , fov_y (60.0f)
          , near_plane (0.001f)
          , far_plane (100.0f) {
        update_camera_vectors ();
    }

    LiteMath::float4x4 get_view_projection_matrix (float aspect_ratio) const {
        return get_projection_matrix (aspect_ratio) * get_view_matrix ();
    }

    LiteMath::float4x4 get_view_matrix () const {
        return LiteMath::lookAt (camera_position, camera_position + camera_front, camera_up);
    }

    LiteMath::float4x4 get_projection_matrix (float aspect_ratio) const {
        return LiteMath::perspectiveMatrix (fov_y, aspect_ratio, near_plane, far_plane);
    }

    void process_keyboard_input (Camera_Movement direction, float delta_time) {
        float velocity = movement_speed * delta_time;
        if (direction == FORWARD)
            camera_position += camera_front * velocity;
        if (direction == BACKWARD)
            camera_position -= camera_front * velocity;
        if (direction == LEFT)
            camera_position -= camera_right * velocity;
        if (direction == RIGHT)
            camera_position += camera_right * velocity;
        if (direction == UP)
            camera_position += LiteMath::float3 (0.0f, 1.0f, 0.0f) * velocity;
        if (direction == DOWN)
            camera_position -= LiteMath::float3 (0.0f, 1.0f, 0.0f) * velocity;
    }

    void process_mouse_movement (float x_offset, float y_offset, bool constrain_pitch = true) {
        x_offset *= mouse_sensitivity;
        y_offset *= mouse_sensitivity;

        yaw_angle -= x_offset;
        pitch_angle += y_offset;

        if (constrain_pitch) {
            if (pitch_angle > 89.0f)
                pitch_angle = 89.0f;
            if (pitch_angle < -89.0f)
                pitch_angle = -89.0f;
        }

        update_camera_vectors ();
    }

    void update_camera_vectors () {
        LiteMath::float3 new_front;
        new_front.x = std::cos (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);
        new_front.y = std::sin (LiteMath::DEG_TO_RAD * pitch_angle);
        new_front.z = std::sin (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);

        camera_front = LiteMath::normalize (new_front);
        camera_right = LiteMath::normalize (LiteMath::cross (camera_front, LiteMath::float3 (0.0f, -1.0f, 0.0f)));
        camera_up    = LiteMath::normalize (LiteMath::cross (camera_right, camera_front));
    }

    static std::vector<LiteMath::float4> extract_frustum_planes (const LiteMath::float4x4& view_projection_matrix) {
        std::vector<LiteMath::float4> planes (6);

        LiteMath::float4 c0 = view_projection_matrix.col (0);
        LiteMath::float4 c1 = view_projection_matrix.col (1);
        LiteMath::float4 c2 = view_projection_matrix.col (2);
        LiteMath::float4 c3 = view_projection_matrix.col (3);

        planes[0] = c3 - c0; // Right Plane
        planes[1] = c3 + c0; // Left Plane
        planes[2] = c3 + c1; // Bottom Plane
        planes[3] = c3 - c1; // Top Plane
        planes[4] = c2;      // Near Plane (Z_clip >= 0)
        planes[5] = c3 - c2; // Far Plane (Z_clip <= W_clip)

        for ( int i = 0; i < 6; ++i ) {
            float length = LiteMath::length (LiteMath::float3 (planes[i].x, planes[i].y, planes[i].z));
            if ( length < LiteMath::EPSILON ) {
                continue; 
            }
            planes[i] = planes[i] / length;
        }
        return planes;
    }

    bool is_sphere_in_frustum (const LiteMath::float3& sphere_center
            , float sphere_radius 
            , const std::vector <LiteMath::float4>& frustum_planes) const {
        for (const auto& plane : frustum_planes) {
            float distance = plane.x * sphere_center.x
                + plane.y * sphere_center.y
                + plane.z * sphere_center.z
                + plane.w;

            if (distance < -sphere_radius) {
                return false;
            }
        }
        return true;
    }

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

