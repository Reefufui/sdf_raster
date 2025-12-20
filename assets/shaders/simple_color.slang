#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[shader("fragment")]
float4 main (Vertex input) : SV_Target {
    if (pc.frame_stack_offset == 0) {
        return float4 (1.0, 0.0f, 0.0f, 1.0f);
    } else if (pc.frame_stack_offset == MAX_OCTREE_DEPTH) {
        return float4 (0.0, 1.0f, 0.0f, 1.0f);
    } else if (pc.frame_stack_offset == 2 * MAX_OCTREE_DEPTH) {
        return float4 (0.0, 0.0f, 1.0f, 1.0f);
    }
    return input.color;
}

