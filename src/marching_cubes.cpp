#include <iostream>

#include "omp.h"

#include "marching_cubes_lookup_table.hpp"
#include "marching_cubes.hpp"

namespace sdf_raster {

struct VoxelInfo {
    LiteMath::float3 min_corner;
    float voxel_size;
    const float (*sdf_values)[8];
};

struct NodeContext {
    const SdfOctreeNode* node;
    VoxelInfo voxel_info;
};

struct ThreadLocalBucket {
    std::vector <VoxelInfo> found_leaves;
    std::vector <NodeContext> children_contexts;
};

std::vector <NodeContext> init_octree_root_context (const SdfOctreeNode* root) {
    NodeContext root_context;
    root_context.node = root;
    root_context.voxel_info.voxel_size = 2.f;
    root_context.voxel_info.min_corner = {-1.0f, -1.0f, -1.0f};
    root_context.voxel_info.sdf_values = nullptr;
    return {root_context};
}

std::vector <VoxelInfo> collect_all_leaf_info (const SdfOctree& scene) {
    if (scene.nodes.empty ()) {
        throw std::runtime_error {"[collect_all_leaf_info]: empty sdf"};
    }

    std::vector <NodeContext> current_level_contexts = init_octree_root_context (&scene.nodes [0]);
    std::vector <VoxelInfo> all_leaf_info;

    while (!current_level_contexts.empty ()) {
        std::vector <ThreadLocalBucket> thread_local_bucket (omp_get_max_threads ());

        #pragma omp parallel
        {
            int thread_id = omp_get_thread_num ();

            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < current_level_contexts.size (); ++i) {
                const NodeContext& current_context = current_level_contexts [i];

                if (current_context.node->offset == 0) {
                    VoxelInfo leaf_info = current_context.voxel_info;
                    leaf_info.sdf_values = &(current_context.node->values);
                    thread_local_bucket [thread_id].found_leaves.push_back (leaf_info);
                } else {
                    float child_voxel_size = current_context.voxel_info.voxel_size * 0.5f;

                    for (unsigned int k = 0; k < 8; ++k) {
                        size_t child_index = current_context.node->offset + k;
                        if (child_index >= scene.nodes.size ()) {
                            throw std::runtime_error {"[collect_all_leaf_info]: out of bounds."};
                        }

                        LiteMath::float3 corner_offset = {0.0f, 0.0f, 0.0f};
                        if ((k >> 0) & 1) corner_offset.x = child_voxel_size;
                        if ((k >> 1) & 1) corner_offset.y = child_voxel_size;
                        if ((k >> 2) & 1) corner_offset.z = child_voxel_size;

                        NodeContext child_context;
                        child_context.node = &scene.nodes [child_index];
                        child_context.voxel_info.min_corner = current_context.voxel_info.min_corner + corner_offset;
                        child_context.voxel_info.voxel_size = child_voxel_size;
                        child_context.voxel_info.sdf_values = nullptr;
                        thread_local_bucket [thread_id].children_contexts.push_back (child_context);
                    }
                }
            }
        }

        size_t total_leaves_found_this_level = 0;
        for (int tid = 0; tid < omp_get_max_threads (); ++tid) {
            total_leaves_found_this_level += thread_local_bucket [tid].found_leaves.size ();
        }
        all_leaf_info.reserve (all_leaf_info.size () + total_leaves_found_this_level);

        for (int tid = 0; tid < omp_get_max_threads (); ++tid) {
            all_leaf_info.insert (all_leaf_info.end ()
                                   , thread_local_bucket [tid].found_leaves.begin ()
                                   , thread_local_bucket [tid].found_leaves.end ()
                                   );
        }

        std::vector <NodeContext> next_level_contexts;
        for (int tid = 0; tid < omp_get_max_threads (); ++tid) {
            next_level_contexts.insert (next_level_contexts.end ()
                                     , thread_local_bucket [tid].children_contexts.begin ()
                                     , thread_local_bucket [tid].children_contexts.end ()
                                     );
        }

        current_level_contexts = std::move (next_level_contexts);
    }

    return all_leaf_info;
}

LiteMath::float3 interpolate_vertex (float isolevel, LiteMath::float3 p1, LiteMath::float3 p2, float valp1, float valp2) {
    if (std::abs (isolevel - valp1) < 0.00001)
        return (p1);
    if (std::abs (isolevel - valp2) < 0.00001)
        return (p2);
    if (std::abs (valp1 - valp2) < 0.00001)
        return (p1);

    float mu = (isolevel - valp1) / (valp2 - valp1);

    LiteMath::float3 p;
    p.x = p1.x + mu * (p2.x - p1.x);
    p.y = p1.y + mu * (p2.y - p1.y);
    p.z = p1.z + mu * (p2.z - p1.z);
    return (p);
}

void process_leaf_node (const VoxelInfo& voxel_info, Mesh& mesh, const float iso_level) {
    // 0: (min_x, min_y, min_z)
    // 1: (max_x, min_y, min_z)
    // 2: (max_x, max_y, min_z)
    // 3: (min_x, max_y, min_z)
    // 4: (min_x, min_y, max_z)
    // 5: (max_x, min_y, max_z)
    // 6: (max_x, max_y, max_z)
    // 7: (min_x, max_y, max_z)
    LiteMath::float3 corners [8];
    corners [0] = voxel_info.min_corner;
    corners [1] = {voxel_info.min_corner.x + voxel_info.voxel_size, voxel_info.min_corner.y, voxel_info.min_corner.z};
    corners [2] = {voxel_info.min_corner.x + voxel_info.voxel_size, voxel_info.min_corner.y + voxel_info.voxel_size, voxel_info.min_corner.z};
    corners [3] = {voxel_info.min_corner.x, voxel_info.min_corner.y + voxel_info.voxel_size, voxel_info.min_corner.z};
    corners [4] = {voxel_info.min_corner.x, voxel_info.min_corner.y, voxel_info.min_corner.z + voxel_info.voxel_size};
    corners [5] = {voxel_info.min_corner.x + voxel_info.voxel_size, voxel_info.min_corner.y, voxel_info.min_corner.z + voxel_info.voxel_size};
    corners [6] = {voxel_info.min_corner.x + voxel_info.voxel_size, voxel_info.min_corner.y + voxel_info.voxel_size, voxel_info.min_corner.z + voxel_info.voxel_size};
    corners [7] = {voxel_info.min_corner.x, voxel_info.min_corner.y + voxel_info.voxel_size, voxel_info.min_corner.z + voxel_info.voxel_size};
    const auto& corner_values = *voxel_info.sdf_values;

    int cube_index = 0;
    for (int i = 0; i < 8; ++i) {
        if (corner_values [i] < iso_level) {
            cube_index |= (1 << i);
        }
    }
    int edge_mask = edgeTable [cube_index];
    if (edge_mask == 0) {
        return;
    }

    LiteMath::float3 vertlist [12];
    if (edge_mask & 1)
        vertlist[0] = interpolate_vertex (iso_level, corners [0], corners [1], corner_values [0], corner_values [1]);
    if (edge_mask & 2)
        vertlist[1] = interpolate_vertex (iso_level, corners [1], corners [2], corner_values [1], corner_values [2]);
    if (edge_mask & 4)
        vertlist[2] = interpolate_vertex (iso_level, corners [2], corners [3], corner_values [2], corner_values [3]);
    if (edge_mask & 8)
        vertlist[3] = interpolate_vertex (iso_level, corners [3], corners [0], corner_values [3], corner_values [0]);
    if (edge_mask & 16)
        vertlist[4] = interpolate_vertex (iso_level, corners [4], corners [5], corner_values [4], corner_values [5]);
    if (edge_mask & 32)
        vertlist[5] = interpolate_vertex (iso_level, corners [5], corners [6], corner_values [5], corner_values [6]);
    if (edge_mask & 64)
        vertlist[6] = interpolate_vertex (iso_level, corners [6], corners [7], corner_values [6], corner_values [7]);
    if (edge_mask & 128)
        vertlist[7] = interpolate_vertex (iso_level, corners [7], corners [4], corner_values [7], corner_values [4]);
    if (edge_mask & 256)
        vertlist[8] = interpolate_vertex (iso_level, corners [0], corners [4], corner_values [0], corner_values [4]);
    if (edge_mask & 512)
        vertlist[9] = interpolate_vertex (iso_level, corners [1], corners [5], corner_values [1], corner_values [5]);
    if (edge_mask & 1024)
        vertlist[10] = interpolate_vertex (iso_level, corners [2], corners [6], corner_values [2], corner_values [6]);
    if (edge_mask & 2048)
        vertlist[11] = interpolate_vertex (iso_level, corners [3], corners [7], corner_values [3], corner_values [7]);

    const int *edges = triTable [cube_index];
    for (int i = 0; edges [i] != -1; ++i) {
        Vertex vertex;
        vertex.position = vertlist [edges [i]];
        vertex.color = LiteMath::float3 {1.0f};
        mesh.add_vertex_fast (vertex);
    }
}

std::vector <Mesh> create_mesh_marching_cubes (const MarchingCubesSettings settings, const SdfOctree& scene) {
    const auto previous_num_threads = omp_get_max_threads ();
    omp_set_num_threads (settings.max_threads);
    const auto leaves = collect_all_leaf_info (scene);
    printf ("SDF-Octree leaves count: %u\n", (unsigned) leaves.size ());

    std::vector <Mesh> thread_meshes (settings.max_threads);
    #pragma omp parallel
    {
        auto& current_thread_mesh = thread_meshes [omp_get_thread_num ()];

        #pragma omp for schedule (dynamic) nowait
        for (size_t i = 0; i < leaves.size (); ++i) {
            process_leaf_node (leaves [i], current_thread_mesh, settings.iso_level);
        }
    }

    for (int tid = 0; tid < settings.max_threads; ++tid) {
        printf ("Marching Cubes [%u]: %u vertices, %u triangles\n"
                , (unsigned) tid
                , (unsigned) thread_meshes [tid].get_vertices ().size ()
                , (unsigned) thread_meshes [tid].get_indices ().size () / 3
                );
    }

    omp_set_num_threads (previous_num_threads);
    return thread_meshes;
}

}
