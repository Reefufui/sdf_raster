#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::input_attachment_index (0)]] SubpassInput input_position;
[[vk::input_attachment_index (1)]] SubpassInput input_normal;
[[vk::input_attachment_index (2)]] SubpassInput input_albedo;

[shader ("fragment")]
float4 main () : SV_Target {
    const float3 position = input_position.SubpassLoad ().xyz;
    const float3 normal   = input_normal.SubpassLoad ().xyz;
    const float4 albedo   = input_albedo.SubpassLoad ();

    float4 output;

    const float4 light_position = float4 (10.0f, 10.0f, 10.0f, 1.0f);
    const float4 light_color = float4 (1.0f, 1.0f, 1.0f, 1.0f);
    const float4 ambient_intensity = float4 (0.1f, 0.1f, 0.1f, 1.0f);
    const float  diffuse_intensity_factor = 1.0f;
    const float  specular_intensity_factor = 0.5f;
    const float  shininess = 32.0f;

    const float3 light_dir = normalize (light_position.xyz - position.xyz);
    const float3 view_dir = normalize (pc.camera_pos.xyz - position.xyz);
    const float4 ambient_term = albedo * ambient_intensity;
    const float diffuse_factor = saturate (dot (normal, light_dir));
    const float4 diffuse_term = light_color * albedo * diffuse_factor * diffuse_intensity_factor;

    const float3 R = reflect (-light_dir, normal);

    const float specular_factor = pow (saturate (dot (R, view_dir)), shininess);
    const float4 specular_term = light_color * specular_factor * specular_intensity_factor;

    return ambient_term + diffuse_term + specular_term;
}

