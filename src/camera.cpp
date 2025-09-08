#include <algorithm>

#include "camera.hpp"

namespace sdf_raster {

Camera::Camera(LiteMath::float3 position, LiteMath::float3 look_at, LiteMath::float3 up, float fov_deg, float aspect_ratio, float near_plane, float far_plane)
    : position_(position),
      look_at_(look_at),
      up_(up),
      fov_deg_(fov_deg),
      aspect_ratio_(aspect_ratio),
      near_plane_(near_plane),
      far_plane_(far_plane) {}

void Camera::update(GLFWwindow* window, float /*delta_time*/) {
    // Получение текущих координат курсора
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);

    if (first_mouse_) {
        last_mouse_x_ = xpos;
        last_mouse_y_ = ypos;
        first_mouse_ = false;
    }

    // Вычисление смещения
    float xoffset = static_cast<float>(xpos - last_mouse_x_);
    float yoffset = static_cast<float>(last_mouse_y_ - ypos); // Y-координаты инвертированы для движения вверх/вниз
    last_mouse_x_ = xpos;
    last_mouse_y_ = ypos;

    xoffset *= mouse_sensitivity_;
    yoffset *= mouse_sensitivity_;

    yaw_ += xoffset;
    pitch_ += yoffset;

    // Ограничение тангажа, чтобы избежать "переворота" камеры
    pitch_ = std::clamp(pitch_, -89.0f, 89.0f);

    // Вычисление нового "переднего" вектора на основе углов Эйлера
    LiteMath::float3 front;
    front.x = cos(LiteMath::DEG_TO_RAD * yaw_) * cos(LiteMath::DEG_TO_RAD * pitch_);
    front.y = sin(LiteMath::DEG_TO_RAD * pitch_);
    front.z = sin(LiteMath::DEG_TO_RAD * yaw_) * cos(LiteMath::DEG_TO_RAD * pitch_);
    look_at_ = position_ + LiteMath::normalize(front);
}

void Camera::set_aspect_ratio(float aspect_ratio) {
    aspect_ratio_ = aspect_ratio;
}

LiteMath::float4x4 Camera::get_view_matrix() const {
    return LiteMath::lookAt(position_, look_at_, up_);
}

LiteMath::float4x4 Camera::get_projection_matrix() const {
    return LiteMath::perspectiveMatrix(LiteMath::DEG_TO_RAD * fov_deg_, aspect_ratio_, near_plane_, far_plane_);
}

}

