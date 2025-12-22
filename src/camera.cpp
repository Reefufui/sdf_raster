#include <fstream>
#include <iostream>

#include "nlohmann/json.hpp"
#include "camera.hpp"

namespace sdf_raster {

Camera::Camera (LiteMath::float3 initial_position , LiteMath::float3 initial_up , float initial_yaw , float initial_pitch)
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

LiteMath::float4x4 Camera::get_view_projection_matrix (float aspect_ratio) const {
    return get_projection_matrix (aspect_ratio) * get_view_matrix ();
}

LiteMath::float4x4 Camera::get_view_matrix () const {
    return LiteMath::lookAt (camera_position, camera_position + camera_front, camera_up);
}

LiteMath::float4x4 Camera::get_projection_matrix (float aspect_ratio) const {
    return LiteMath::perspectiveMatrix (fov_y, aspect_ratio, near_plane, far_plane);
}

void Camera::process_keyboard_input (Camera_Movement direction, float delta_time) {
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

void Camera::process_mouse_movement (float x_offset, float y_offset, bool constrain_pitch) {
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

void Camera::update_camera_vectors () {
    LiteMath::float3 new_front;
    new_front.x = std::cos (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);
    new_front.y = std::sin (LiteMath::DEG_TO_RAD * pitch_angle);
    new_front.z = std::sin (LiteMath::DEG_TO_RAD * yaw_angle) * std::cos (LiteMath::DEG_TO_RAD * pitch_angle);

    camera_front = LiteMath::normalize (new_front);
    camera_right = LiteMath::normalize (LiteMath::cross (camera_front, LiteMath::float3 (0.0f, -1.0f, 0.0f)));
    camera_up    = LiteMath::normalize (LiteMath::cross (camera_right, camera_front));
}

std::vector <LiteMath::float4> Camera::extract_frustum_planes (const LiteMath::float4x4& view_projection_matrix) {
    std::vector <LiteMath::float4> planes (6);

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

bool Camera::is_sphere_in_frustum (const LiteMath::float3& sphere_center
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
    j ["near_plane"] = near_plane;
    j ["far_plane"] = far_plane;

    std::ofstream o (filename);
    if (o.is_open ()) {
        o << std::setw (4) << j << std::endl;
        o.close();
    } else {
        std::cerr << "Error: Could not open file for dumping camera settings: " << filename << std::endl;
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
            near_plane = j.at ("near_plane").get <float> ();
            far_plane = j.at ("far_plane").get <float> ();

            update_camera_vectors ();

        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error parsing camera settings from JSON: " << e.what () << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "General error loading camera settings: " << e.what () << std::endl;
        }
        i.close ();
    } else {
        std::cerr << "Warning: Could not open file for loading camera settings: " << filename << ". Using default camera settings." << std::endl;
    }
}

}
