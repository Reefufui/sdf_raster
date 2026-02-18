// frustrum_vert.slang

#include "common.h"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

struct VertexStageOutput {
    float4 sv_position : SV_Position;
};

[shader ("vertex")]
VertexStageOutput main (float3 position) {
    VertexStageOutput output;
    output.sv_position = mul (pc.view_proj, float4 (position, 1.f));
    return output;
}
