#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

[shader("fragment")]
float4 main (Vertex input) : SV_Target
{
    // float4 color = input.color;
    return float4 (1.0f, 1.0f, 0.0f, 1.0f);
}
