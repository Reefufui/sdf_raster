// frustum_view.vert.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

struct VertexStageOutput {
    float4 sv_position : SV_Position;
};

[shader ("vertex")]
VertexStageOutput main (float4 position) {
    VertexStageOutput output;
    output.sv_position = mul (pc.view_proj, position);
    return output;
}

