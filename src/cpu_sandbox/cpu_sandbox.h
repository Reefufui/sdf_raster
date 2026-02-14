#include <iostream>
#include <LiteMath.h>

namespace cpu_sandbox {

#define MESH_WORKGROUP_SIZE 16
#define MAX_VERTS_PER_MESHLET (MESH_WORKGROUP_SIZE * 12)
#define MAX_PRIMS_PER_MESHLET (MESH_WORKGROUP_SIZE * 5)

#define MAX_LEAF_VERTS 12
#define MAX_LEAF_PRIMS 4
#define MAX_OCTREE_DEPTH 13 // 16 - 3

using float4x4 = LiteMath::float4x4;
using float4 = LiteMath::float4;
using float3 = LiteMath::float3;
using uint2 = LiteMath::uint2;
using uint3 = LiteMath::uint3;
using uint = unsigned int;

struct Vertex {
    float4 position;
    float4 color;
};

struct NodeContext {
    float3 min_corner;
    float voxel_size;
    int node_index;
    int cube_index;
};

struct LeafContext {
    NodeContext node_context;
    uint vertices_local_offset;
    uint triangles_local_offset;
};

struct TaskPayload {
    uint leaf_buffer_offset;
    uint leaf_count;
    uint vertices_count;
    uint triangles_count;
};

struct PushConstantsData {
    float4x4 view_proj;
    float4 camera_pos;
    float4 color;
    float4 frustum_planes [6];
    int max_octree_depth;
    uint max_count_per_task; // leaf/vertex count (depends on -mode <mesh>|<compute>)
};

struct SdfOctreeNode {
  float values [8];
  uint offset; // offset for children (they are stored together). 0 offset means it's a leaf
};

void dispatch_mesh (NodeContext leaf, std::vector <SdfOctreeNode>& nodes);

void task_generator (NodeContext root, std::vector <SdfOctreeNode>& nodes);

void add_vertex (float4 position);

void add_triangle (uint3 indices);

size_t get_vertex_count ();

void dump_obj (const std::string& filename);

}

