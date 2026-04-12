//fullscreen_quad.vert.slang

struct VertexOutput {
    float4 position : SV_Position;
};

[shader ("vertex")]
VertexOutput main (uint vertexID : SV_VertexID) {
    VertexOutput output;
    float2 uv = float2 ((vertexID << 1) & 2, vertexID & 2);
    output.position = float4 (uv * 2.0f - 1.0f, 0.0f, 1.0f);
    output.position.y = -output.position.y; 
    return output;
}

