#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus

#include <LiteMath.h>

#define column_major

using float3 = LiteMath::float3;
using float4 = LiteMath::float4;
using float4x4 = LiteMath::float4x4;

#else

struct Payload {
    float3 min_corner;
    float voxel_size;
    int node_index;
};

struct Vertex {
    float4 position : SV_Position;
    float4 color : Color;
};

#endif // __cplusplus

struct PushConstantsData {
    column_major float4x4 view_proj;
    float3 camera_pos;
    float padding;
    float4 color;
    float4 frustum_planes [6];
};

struct SdfOctreeNode {
  float values [8];
  uint offset; // offset for children (they are stored together). 0 offset means it's a leaf
};

#endif // COMMON_H

