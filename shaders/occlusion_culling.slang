// occlusion_culling.slang

bool voxel_occluded (Sampler2D <float> depth_sampler, float4x4 depth_view_proj, const float3 voxel_corners [8]) {
    float2 min_uv = float2 (1.0f);
    float2 max_uv = float2 (0.0f);
    float min_depth = 1.0f;

    for (int i = 0; i < 8; ++i) {
        float4 clip = mul (depth_view_proj, float4 (voxel_corners [i], 1.0f));
        if (clip.w <= 0.0f) {
            return false;
        }

        float3 ndc = clip.xyz / clip.w;
        float2 uv = ndc.xy * 0.5f + 0.5f;

        min_uv = min (min_uv, uv);
        max_uv = max (max_uv, uv);
        min_depth = min (min_depth, ndc.z);
    }

    if (max_uv.x <= 0.0f || max_uv.y <= 0.0f || min_uv.x >= 1.0f || min_uv.y >= 1.0f) {
        return false; // NOTE: Using previous frame's view matrix for frustum culling is a bad idea.
    }

    min_uv = clamp (min_uv, 0.0f, 1.0f);
    max_uv = clamp (max_uv, 0.0f, 1.0f);

    uint mip_w, mip_h, mip_levels;
    depth_sampler.GetDimensions (0, mip_w, mip_h, mip_levels);

    float2 aabb_pixels = float2 (max_uv - min_uv) * float2 (mip_w, mip_h);
    float max_dim = max (aabb_pixels.x, aabb_pixels.y);

    uint mip = (uint) floor (log2 (floor (max_dim))); // NOTE: On this mip level: texel size >= AABB size
    mip = clamp (mip, 0u, mip_levels);

    float hz0 = depth_sampler.SampleLevel (float2 (min_uv.x, min_uv.y), mip);
    float hz1 = depth_sampler.SampleLevel (float2 (max_uv.x, min_uv.y), mip);
    float hz2 = depth_sampler.SampleLevel (float2 (min_uv.x, max_uv.y), mip);
    float hz3 = depth_sampler.SampleLevel (float2 (max_uv.x, max_uv.y), mip);

    float hz_depth = max (max (hz0, hz1), max (hz2, hz3));
    return min_depth > hz_depth;
}

