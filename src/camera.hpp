#pragma once

#include "GLFW/glfw3.h"
#include "LiteMath.h"

class Camera {
public:
    Camera(LiteMath::float3 position, LiteMath::float3 look_at, LiteMath::float3 up, float fov_deg, float aspect_ratio, float near_plane, float far_plane);

    void update(GLFWwindow* window, float delta_time);
    void set_aspect_ratio(float aspect_ratio);

    LiteMath::float4x4 get_view_matrix() const;
    LiteMath::float4x4 get_projection_matrix() const;
    LiteMath::float3 get_position() const { return position_; }

private:
    LiteMath::float3 position_;
    LiteMath::float3 look_at_;
    LiteMath::float3 up_; // Up vector for view matrix

    float fov_deg_;
    float aspect_ratio_;
    float near_plane_;
    float far_plane_;

    // Параметры для управления мышью
    double last_mouse_x_;
    double last_mouse_y_;
    bool first_mouse_ = true;
    float yaw_ = -90.0f; // Начальный рыскание (yaw), чтобы смотреть вперед
    float pitch_ = 0.0f; // Начальный тангаж (pitch)
    float mouse_sensitivity_ = 0.1f;
};

