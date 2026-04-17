// blinn_phong.frag.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[shader("fragment")]
float4 main (Vertex input : FragmentInput) : SV_Target {
    const float4 light_position = float4 (10.0f, 10.0f, 10.0f, 1.0f);
    const float4 light_color = float4 (1.0f, 1.0f, 1.0f, 1.0f);

    const float4 ambient_intensity = float4 (0.1f, 0.1f, 0.1f, 1.0f);
    const float  diffuse_intensity_factor = 1.0f;
    const float  specular_intensity_factor = 0.5f;
    const float  shininess = 32.0f;

    float3 N = (input.normal.xyz);
    float3 light_dir = normalize (light_position.xyz - input.position.xyz);
    float3 view_dir = normalize (pc.camera_pos.xyz - input.position.xyz);
    float4 ambient_term = input.color * ambient_intensity;
    float diffuse_factor = saturate (dot (N, light_dir));
    float4 diffuse_term = light_color * input.color * diffuse_factor * diffuse_intensity_factor;

    float3 R = reflect (-light_dir, N);

    float specular_factor = pow (saturate (dot (R, view_dir)), shininess);
    float4 specular_term = light_color * specular_factor * specular_intensity_factor;

    // return ambient_term + diffuse_term + specular_term;
    return ambient_term + diffuse_term;
}

