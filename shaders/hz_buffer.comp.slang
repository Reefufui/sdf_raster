// hz_buffer.comp.slang

#include "shader_common.hpp"

[[vk::binding (0, 0)]] RWTexture2D <float> src_texture;
[[vk::binding (1, 0)]] RWTexture2D <float> dst_texture;

[shader ("compute")]
[numthreads (16, 16, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
    const uint2 dst_coord = thread_id.xy;

    uint lvl;
    uint2 dst_dim;
    dst_texture.GetDimensions (0, dst_dim.x, dst_dim.y, lvl);
    if (dst_coord.x >= dst_dim.x) return;
    if (dst_coord.y >= dst_dim.y) return;

    uint2 src_dim;
    src_texture.GetDimensions (0, src_dim.x, src_dim.y, lvl);

    const int2 src_coord_center = int2 (ceil ((float2 (dst_coord) / float2 (dst_dim)) * float2 (src_dim)));
    float max_depth = 0.0f;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 src_coord = src_coord_center + int2 (x, y);
            src_coord.x = clamp (src_coord.x, 0, src_dim.x - 1);
            src_coord.y = clamp (src_coord.y, 0, src_dim.y - 1);

            max_depth = max (max_depth, src_texture [src_coord]);
        }
    }

    dst_texture [dst_coord] = max_depth;
}

