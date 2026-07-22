// view_proj.vert.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

struct VertexStageOutput {
    Vertex frament_input : FragmentInput;
    float4 sv_position : SV_Position;
};

[shader ("vertex")]
VertexStageOutput main (Vertex input) {
    VertexStageOutput output;
    output.sv_position = mul (pc.mvp, input.position);
    output.frament_input.position = mul (pc.model_matrix, input.position);
    output.frament_input.normal = mul (pc.model_matrix, input.normal);
    output.frament_input.color = input.color;
    return output;
}

