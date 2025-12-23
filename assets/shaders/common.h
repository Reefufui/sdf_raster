#ifndef COMMON_H
#define COMMON_H

#define MAX_OCTREE_DEPTH 20
#define MAX_LEAF_VERTS 12
#define MAX_LEAF_PRIMS 4
#define MAX_MESH_ID 1024

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

struct Payload {
    float3 min_corner;
    float voxel_size;
    int node_index;
    int cube_index;
    int mesh_id;
};

struct PushConstantsData {
    column_major float4x4 view_proj;
    float3 camera_pos;
    float padding;
    float4 color;
    float4 frustum_planes [6];
    int frame_stack_offset;
};

struct SdfOctreeNode {
  float values [8];
  uint offset; // offset for children (they are stored together). 0 offset means it's a leaf
};

#endif // COMMON_H

