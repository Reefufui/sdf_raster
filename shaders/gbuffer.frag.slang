// gbuffer.frag.slang

#include "shader_common.hpp"

struct GBufferOutput {
    float4 position : SV_Target0;
    float4 normal   : SV_Target1;
    float4 albedo   : SV_Target2;
};

[shader ("fragment")]
GBufferOutput main (Vertex input : FragmentInput) {
    GBufferOutput output;
    output.position = float4 (input.position.xyz, 1.0);
    output.normal   = float4 (normalize (input.normal.xyz), 1.0);
    output.albedo   = input.color; 
    return output;
}

