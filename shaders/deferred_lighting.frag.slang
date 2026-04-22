// deferred_lighting.frag.slang
#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[[vk::binding (0, 0)]] Sampler2D tex_position;
[[vk::binding (1, 0)]] Sampler2D tex_normal;
[[vk::binding (2, 0)]] Sampler2D tex_albedo;
[[vk::binding (3, 0)]] Sampler2D tex_depth;

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

    const float depthThreshold = 0.0001f; 
    
    float bestNeighborDepth = 1.0f;
    int2  best_offset = int2 (0, 0);
    bool  isHole = false;

    [unroll]
    for (int y = -2; y <= 2; y++) {
        [unroll]
        for (int x = -2; x <= 2; x++) {
            if (x == 0 && y == 0) continue;

            float2 sample_uv = uv + float2 (x, y) * texel_size;
            float d = tex_depth.Sample (sample_uv).r;

            if (d < depth - depthThreshold) {
                isHole = true;
                if (d < bestNeighborDepth) {
                    bestNeighborDepth = d;
                    best_offset = int2 (x, y);
                }
            }
        }
    }

    if (isHole) {
        float2 offset_uv = uv + float2 (best_offset) * texel_size;
        normal = tex_normal.Sample (offset_uv).xyz;
        position = tex_position.Sample (offset_uv).xyz;
        albedo = tex_albedo.Sample (offset_uv);
        depth = bestNeighborDepth;
    }

    if (depth == 1.f) {
        return albedo;
    }

    const float3 lightPos = float3 (5.0f, 5.0f, 5.0f);
    const float3 lightCol = float3 (1.0f, 1.0f, 1.0f);
    
    float3 light_dir = normalize (lightPos - position);
    float3 view_dir  = normalize (pc.camera_pos.xyz - position);
    float3 half_dir  = normalize (light_dir + view_dir);

    float diff = saturate (dot (normal, light_dir));
    float3 diffuse_term = albedo.rgb * diff * lightCol;

    float spec = pow (saturate (dot (normal, half_dir)), 64.0f);
    float3 specular_term = float3 (0.4f) * spec * lightCol;

    float3 ambient_term = albedo.rgb * 0.1f;

    float3 finalColor = ambient_term + diffuse_term + specular_term;

    const float3 fogColor = float3 (0.25f, 0.25f, 0.25f);
    const float fogStart = 0.999f;
    const float fogEnd = 1.0f;

    float fogFactor = smoothstep (fogStart, fogEnd, depth);

    float3 colorWithFog = lerp (finalColor, fogColor, fogFactor);

    return float4 (colorWithFog, 1.0f);
}

