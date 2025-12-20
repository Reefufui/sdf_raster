#include "../../assets/shaders/common.h"

namespace cpu_sandbox {

using float3 = LiteMath::float3;
using uint2 = LiteMath::uint2;
using uint3 = LiteMath::uint3;
using uint = unsigned int;

struct Vertex {
    float4 position;
    float4 color;
};

#define MAX_OCTREE_DEPTH 16
#define MAX_VERTS 12
#define MAX_PRIMS 4

void dispatch_mesh (Payload payload, std::vector <SdfOctreeNode>& nodes);

void task_generator (Payload subtree, std::vector <SdfOctreeNode>& nodes);

void add_vertex (float4 position);

void add_triangle (uint3 indices);

size_t get_vertex_count ();

void dump_obj (const std::string& filename);

}

