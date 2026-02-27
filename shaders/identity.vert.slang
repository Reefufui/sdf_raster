// identity.vert.slang

#include "shader_common.hpp"

struct VertexStageOutput {
    Vertex frament_input : FragmentInput;
    float4 sv_position : SV_Position;
};

[shader ("vertex")]
VertexStageOutput main (Vertex input) {
    VertexStageOutput output;
    output.sv_position = input.position;
    output.frament_input.position = input.position;
    output.frament_input.normal = input.normal;
    output.frament_input.color = input.color;
    return output;
}

