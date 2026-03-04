// blinn_phong.frag.slang

#include "shader_common.hpp"

[[vk::push_constant]] ConstantBuffer <PushConstantsData> pc;

struct MeshOutput {
    float4 position : SV_Position;
    Vertex vertex;
};

[shader("fragment")]
float4 main (in MeshOutput input) : SV_Target {
    return float4(1, 0, 0, 1);
}

