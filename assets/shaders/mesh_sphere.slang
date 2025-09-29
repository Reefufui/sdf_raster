struct DummyPayload {
    uint data;
};

struct MeshOutput {
    float4 position : SV_Position;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1,1,1)]
void main(
    in payload DummyPayload payload,
    out vertices MeshOutput verts[3],
    out indices uint3 primitives[1])
{
    SetMeshOutputCounts(3, 1);
    
    verts[0].position = float4(-0.5, 0.5, 0, 1);
    verts[1].position = float4(0.5,  0.5, 0, 1);
    verts[2].position = float4(0, -0.5, 0, 1);
    
    primitives[0] = uint3(0, 1, 2);
}

