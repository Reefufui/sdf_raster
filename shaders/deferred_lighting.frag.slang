// deferred_lighting.frag.slang
#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <DeferredLightingPushConstants> pc;

[[vk::binding (0, 0)]] Sampler2D tex_position;
[[vk::binding (1, 0)]] Sampler2D tex_normal;
[[vk::binding (2, 0)]] Sampler2D tex_albedo;
[[vk::binding (3, 0)]] Sampler2D tex_depth;

[[vk::binding (0, 1)]] RWTexture2D <float> hz_buffer;

[shader ("fragment")]
float4 main (float4 sv_pos : SV_Position) : SV_Target {
    uint width, height, mipLevels;
    tex_depth.GetDimensions (0, width, height, mipLevels);
    const float2 uv = sv_pos.xy / float2 (width, height);
    const float2 texel_size = 1.0f / float2 (width, height);

    float depth = tex_depth.Sample (uv).r;
    float3 normal = tex_normal.Sample (uv).xyz;
    float3 position = tex_position.Sample (uv).xyz;
    float4 albedo = tex_albedo.Sample (uv);

    const float similarity_threshold = pc.depth_threshold * 2.0f;

    // 1px Cross samples
    float d_l = tex_depth.Sample (uv + float2 (-1.0f, 0.0f) * texel_size).r;
    float d_r = tex_depth.Sample (uv + float2 (1.0f, 0.0f) * texel_size).r;
    float d_u = tex_depth.Sample (uv + float2 (0.0f, 1.0f) * texel_size).r;
    float d_d = tex_depth.Sample (uv + float2 (0.0f, -1.0f) * texel_size).r;

    // 2px Diagonal samples
    float d_tl = tex_depth.Sample (uv + float2 (-2.0f, 2.0f) * texel_size).r;
    float d_br = tex_depth.Sample (uv + float2 (2.0f, -2.0f) * texel_size).r;
    float d_tr = tex_depth.Sample (uv + float2 (2.0f, 2.0f) * texel_size).r;
    float d_bl = tex_depth.Sample (uv + float2 (-2.0f, -2.0f) * texel_size).r;

    bool hole_x = (depth > min (d_l, d_r) + pc.depth_threshold) && (abs (d_l - d_r) < similarity_threshold);
    bool hole_y = (depth > min (d_u, d_d) + pc.depth_threshold) && (abs (d_u - d_d) < similarity_threshold);
    bool hole_d1 = (depth > min (d_tl, d_br) + pc.depth_threshold) && (abs (d_tl - d_br) < similarity_threshold);
    bool hole_d2 = (depth > min (d_tr, d_bl) + pc.depth_threshold) && (abs (d_tr - d_bl) < similarity_threshold);

    if (hole_x || hole_y || hole_d1 || hole_d2) {
        float min_d = depth;
        float2 best_offset = float2 (0.0f, 0.0f);

        // Priority list: check 1px neighbors first, then 2px diagonals
        float depths [] = {d_l, d_r, d_u, d_d, d_tl, d_br, d_tr, d_bl};
        float2 offsets [] = {
            float2 (-1.0f, 0.0f), float2 (1.0f, 0.0f), float2 (0.0f, 1.0f), float2 (0.0f, -1.0f)
            , float2 (-2.0f, 2.0f), float2 (2.0f, -2.0f), float2 (2.0f, 2.0f), float2 (-2.0f, -2.0f)
        };

        for (int i = 0; i < 8; i++) {
            if (depths [i] < min_d) {
                min_d = depths [i];
                best_offset = offsets [i];
            }
        }

        if (min_d < depth) {
            float2 offset_uv = uv + best_offset * texel_size;
            normal = tex_normal.Sample (offset_uv).xyz;
            position = tex_position.Sample (offset_uv).xyz;
            albedo = tex_albedo.Sample (offset_uv);
            depth = min_d;
        }
    }

    if (pc.enable_hz_write != 0) {
        hz_buffer [int2 (sv_pos.xy)] = depth;
    }

    if (depth == 1.f) {
        return albedo;
    }

    float3 light_dir = normalize (pc.light_pos.xyz - position);
    float3 view_dir  = normalize (pc.camera_pos.xyz - position);
    float3 half_dir  = normalize (light_dir + view_dir);

    float3 ambient_term = albedo.rgb * pc.ambient_strength;

    float diff = max (0.0f, dot (normal, light_dir));
    float3 diffuse_term = albedo.rgb * diff * pc.light_color.xyz;

    float spec = pow (max (0.0f, dot (normal, half_dir)), pc.shininess);
    float3 specular_term = float3 (pc.specular_strength) * spec * pc.light_color.xyz;

    float3 final_color = ambient_term + diffuse_term + specular_term;

    if (pc.fog_end > 0.0f) {
        float fog_factor = smoothstep (pc.fog_start, pc.fog_end, depth);
        final_color = lerp (final_color, pc.fog_color.xyz, fog_factor);
    }

    return float4 (final_color, 1.0f);
}

