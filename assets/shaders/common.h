#ifndef COMMON_H
#define COMMON_H

#define PREFIX_SUM_WORKGROUP_SIZE 256
#define MESH_WORKGROUP_SIZE 16
#define MAX_VERTS_PER_MESHLET (MESH_WORKGROUP_SIZE * 12)
#define MAX_PRIMS_PER_MESHLET (MESH_WORKGROUP_SIZE * 5)

#define MAX_LEAF_VERTS 12
#define MAX_LEAF_INDICES 12
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

struct VkDrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct VkDispatchIndirectCommand {
    uint x;
    uint y;
    uint z;
};

#endif // __cplusplus

struct Vertex {
    float4 position;
    float4 normal;
    float4 color;
};

struct NodeContext {
    float min_corner_x;
    float min_corner_y;
    float min_corner_z;
    float voxel_size;
    int node_index;
    int cube_index;
};

struct TaskPayload {
    uint leaf_buffer_offset;
    uint leaf_count;
    uint vertices_count;
    uint triangles_count;
};

struct PushConstantsData {
    column_major float4x4 view_proj;
    column_major float4x4 view_proj_normal;
    float4 camera_pos;
    int max_octree_depth;
    uint active_leafs_max_count;
};

struct SdfOctreeNode {
    float values [8];
    uint offset; // offset for children (they are stored together). 0 offset means it's a leaf
};

struct FrustumGeometry {
    float4 vertices [8];
    float4 normals [6];
    float4 edges [12];
};

#endif // COMMON_H

