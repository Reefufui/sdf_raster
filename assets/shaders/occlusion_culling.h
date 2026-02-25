// false if fully occluded

bool voxel_occluded (const float3 coord, const float voxel_size) {
    float2 min_uv = float2 (1.0f);
    float2 max_uv = float2 (0.0f);
    float min_depth = 1.0f;

    for (int i = 0; i < 8; ++i) {
        float3 offset = {0.0f, 0.0f, 0.0f};
        if (((i >> 0) & 1) > 0) offset.x = voxel_size;
        if (((i >> 1) & 1) > 0) offset.y = voxel_size;
        if (((i >> 2) & 1) > 0) offset.z = voxel_size;

        float4 clip = mul (pc.prev_view_proj, float4 (coord + offset, 1.0f));
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
        return false;
    }

    min_uv = clamp (min_uv, 0.0f, 1.0f);
    max_uv = clamp (max_uv, 0.0f, 1.0f);

    uint mip_w, mip_h, mip_levels;
    depth_sampler.GetDimensions (0, mip_w, mip_h, mip_levels);

    float2 hzb_resolution = float2 (mip_w, mip_h);
    float2 aabb_pixels = (max_uv - min_uv) * hzb_resolution;
    float max_dim = max (aabb_pixels.x, aabb_pixels.y);

    float mip_f = ceil (log2 (max_dim));
    uint mip = (uint) clamp (mip_f, 0.0f, mip_levels); // NOTE: On this mip level: texel size >= AABB size

    float hz0 = depth_sampler.SampleLevel (float2(min_uv.x, min_uv.y), mip);
    float hz1 = depth_sampler.SampleLevel (float2(max_uv.x, min_uv.y), mip);
    float hz2 = depth_sampler.SampleLevel (float2(min_uv.x, max_uv.y), mip);
    float hz3 = depth_sampler.SampleLevel (float2(max_uv.x, max_uv.y), mip);

    float hz_depth = min (min (hz0, hz1), min (hz2, hz3));
    return min_depth > hz_depth;
}

