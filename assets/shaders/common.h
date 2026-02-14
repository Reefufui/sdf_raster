#ifndef COMMON_H
#define COMMON_H

#define MESH_WORKGROUP_SIZE 16
#define MAX_VERTS_PER_MESHLET (MESH_WORKGROUP_SIZE * 12)
#define MAX_PRIMS_PER_MESHLET (MESH_WORKGROUP_SIZE * 5)

#define MAX_LEAF_VERTS 12
#define MAX_LEAF_PRIMS 4
#define MAX_OCTREE_DEPTH 13 // 16 - 3

#ifdef __cplusplus

#include <LiteMath.h>

#define column_major

using float3 = LiteMath::float3;
using float4 = LiteMath::float4;
using float4x4 = LiteMath::float4x4;
using uint = unsigned int;

#else

struct Vertex {
    float4 position : SV_Position;
    float4 color : Color;
};

#endif // __cplusplus

struct NodeContext {
    float min_corner_x;
    float min_corner_y;
    float min_corner_z;
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
    column_major float4x4 view_proj;
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

#endif // COMMON_H

