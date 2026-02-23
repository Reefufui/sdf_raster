// mip_max_pooling.slang

#include "common.h"

[[vk::binding (0, 0)]] RWTexture2D src_texture;
[[vk::binding (1, 0)]] RWTexture2D dst_texture;

[shader ("compute")]
[numthreads (16, 16, 1)]
void main (uint3 thread_id : SV_DispatchThreadID) {
    const uint x = thread_id.x;
    const uint y = thread_id.y;

    uint dst_w, dst_h, levels;
    dst_texture.GetDimensions (0, dst_w, dst_h, levels);

    if (x >= dst_w) return;
    if (y >= dst_h) return;

    uint src_w, src_h, src_levels;
    src_texture.GetDimensions (0, src_w, src_h, src_levels);

    const uint2 coords0 = uint2 (x * 2, y * 2);
    const uint2 coords1 = uint2 (min (x * 2 + 1, src_w - 1), y * 2);
    const uint2 coords2 = uint2 (x * 2, min (y * 2 + 1, src_h - 1));
    const uint2 coords3 = uint2 (min (x * 2 + 1, src_w - 1), min (y * 2 + 1, src_h - 1));

    const var val0 = src_texture [coords0];
    const var val1 = src_texture [coords1];
    const var val2 = src_texture [coords2];
    const var val3 = src_texture [coords3];

    dst_texture [uint2 (x, y)] = max (max (val0, val1), max (val2, val3));
}

