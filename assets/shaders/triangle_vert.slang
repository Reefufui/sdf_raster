struct VertexInput {
    float4 position : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

[shader ("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    output.position = input.position;
    output.color = input.color;
    return output;
}
