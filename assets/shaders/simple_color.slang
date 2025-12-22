#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[shader("fragment")]
float4 main (Vertex input) : SV_Target {
    return input.color;
}

