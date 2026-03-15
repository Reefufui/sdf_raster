// shader_common.h

#ifndef SHADER_COMMON_H
#define SHADER_COMMON_H

#define VOXELS_PER_COMPUTE_WORKGROUP 256
#define VOXELS_PER_MESH_WORKGROUP 16 // AMD

#ifdef __cplusplus

#include <LiteMath.h>

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

#endif // __cplusplus

static const uint MAX_LEAF_VERTS = 12;
static const uint MAX_LEAF_PRIMS = 4;

struct IndirectDispatch {
    uint x;
    uint y;
    uint z;
};

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

#ifdef __cplusplus
#define column_major
#else
#define alignas(x)
#endif

struct PushConstantsData {
    alignas (16) column_major float4x4 view_proj;
    alignas (16) column_major float4x4 prev_view_proj;
    alignas (16) float4 camera_pos;
    alignas (4)  uint max_octree_depth;
    alignas (4)  uint max_lod;
    alignas (4)  uint subtree_root_level;
    alignas (4)  uint active_leafs_max_count;
    alignas (4)  uint occlusion_culling_level;
    alignas (4)  uint frustum_culling_level;
    alignas (4)  uint color_leafs;
};

struct SdfOctreeNode {
    float values [8]; // NOTE: for each internal node
    uint offset; // NOTE: offset for children (they are stored together). 0 offset means it's a leaf
};

struct FrustumGeometry {
    float4 vertices [8];
    float4 normals [6];
};

struct LevelOfDetail {
    alignas (4) uint max_lod;
    alignas (4) uint min_lod;
    alignas (4) uint lod;
    alignas (4) float max_dim; // NOTE: screen-space size of root voxel in pixels
};

#endif // SHADER_COMMON_H

