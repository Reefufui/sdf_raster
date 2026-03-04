// mesh_sphere.slang

#include "shader_common.hpp"

struct MeshOutput {
    float4 position : SV_Position;
    Vertex vertex;
};

struct DummyPayload {
    uint data;
};

[shader ("mesh")]
[outputtopology ("triangle")]
[numthreads (1, 1, 1)]
void main (
    in payload DummyPayload payload,
    OutputVertices <MeshOutput, MAX_LEAF_VERTS> vertices,
    OutputIndices <uint3, MAX_LEAF_PRIMS> triangles) {
    SetMeshOutputCounts (3, 1);

    vertices [0].position = float4 (-0.5, 0.5, 0, 1);
    vertices [1].position = float4 (0.5,  0.5, 0, 1);
    vertices [2].position = float4 (0, -0.5, 0, 1);

    Vertex vertex;
    vertices [0].vertex.position = float4 (-0.5, 0.5, 0, 1);
    vertices [1].vertex.position = float4 (0.5,  0.5, 0, 1);
    vertices [2].vertex.position = float4 (0, -0.5, 0, 1);
    vertices [0].vertex.color = float4 (1, 0, 0, 1);
    vertices [1].vertex.color = float4 (0, 1, 0, 1);
    vertices [2].vertex.color = float4 (0, 1, 1, 1);

    triangles [0] = uint3 (0, 1, 2);
}

