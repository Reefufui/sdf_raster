#pragma once

#include <LiteMath.h>

namespace sdf_raster::math {

using Quat = LiteMath::float4;

inline Quat quat_mul (const Quat& q1, const Quat& q2) {
    return Quat {
        q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y
        , q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x
        , q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w
        , q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z
    };
}

inline Quat quat_from_axis_angle (const LiteMath::float3& axis, float angle_deg) {
    float angle_rad = angle_deg * LiteMath::DEG_TO_RAD;
    float sin_a = std::sin (angle_rad / 2.0f);
    float cos_a = std::cos (angle_rad / 2.0f);
    LiteMath::float3 norm_axis = LiteMath::normalize (axis);
    return Quat { norm_axis.x * sin_a, norm_axis.y * sin_a, norm_axis.z * sin_a, cos_a };
}

inline LiteMath::float4x4 quat_to_mat4 (const Quat& q) {
    LiteMath::float4x4 m;
    float xx = q.x * q.x;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float xw = q.x * q.w;
    float yy = q.y * q.y;
    float yz = q.y * q.z;
    float yw = q.y * q.w;
    float zz = q.z * q.z;
    float zw = q.z * q.w;

    m.col (0) = LiteMath::float4 { 1.0f - 2.0f * (yy + zz), 2.0f * (xy + zw), 2.0f * (xz - yw), 0.0f };
    m.col (1) = LiteMath::float4 { 2.0f * (xy - zw), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + xw), 0.0f };
    m.col (2) = LiteMath::float4 { 2.0f * (xz + yw), 2.0f * (yz - xw), 1.0f - 2.0f * (xx + yy), 0.0f };
    m.col (3) = LiteMath::float4 { 0.0f, 0.0f, 0.0f, 1.0f };
    return m;
}

} // namespace sdf_raster::math
