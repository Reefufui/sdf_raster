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

LiteMath::float3 estimate_normal (const SdfOctree& scene, const LiteMath::float3& p, float eps = 1e-4f) {
    float dx = sample_sdf (scene, {p.x + eps, p.y, p.z}) - sample_sdf (scene, {p.x - eps, p.y, p.z});
    float dy = sample_sdf (scene, {p.x, p.y + eps, p.z}) - sample_sdf (scene, {p.x, p.y - eps, p.z});
    float dz = sample_sdf (scene, {p.x, p.y, p.z + eps}) - sample_sdf (scene, {p.x, p.y, p.z - eps});
    LiteMath::float3 n = {dx, dy, dz};
    return LiteMath::normalize (n);
}

void process_leaf_node (const VoxelInfo& voxel_info , Mesh& mesh , const float iso_level , const SdfOctree& scene) {
    float corner_values [8];
    for (int i = 0; i < 8; ++i) {
        corner_values [i] = (*voxel_info.sdf_values) [i];
    }

    const LiteMath::float3 voxel_size_modifier {voxel_info.voxel_size};
    int cube_index = 0;
    LiteMath::float3 corners [8];

    for (int i = 0; i < 8; ++i) {
        LiteMath::float3 corner_offset = {0.0f, 0.0f, 0.0f};
        if ((i >> 0) & 1) corner_offset.x = voxel_info.voxel_size;
        if ((i >> 1) & 1) corner_offset.y = voxel_info.voxel_size;
        if ((i >> 2) & 1) corner_offset.z = voxel_info.voxel_size;
        corners [i] = voxel_info.min_corner + corner_offset;

        if (corner_values [i] < iso_level) {
            cube_index |= (1 << i);
        }
    }

    int edge_mask = cube_index_2_edge_mask [cube_index];
    if (edge_mask == 0) {
        return;
    }

    LiteMath::float3 edge_vertices [12];
    int edge_bit = 1;
    for (int i = 0; i < 12; ++i) {
        // if ((edge_mask & edge_bit) == 0) {
        //     continue;
        // }

        const auto corner_indices = edge_corners [i];
        edge_vertices [i] = interpolate_vertex (iso_level
                                                , corners [corner_indices.x]
                                                , corners [corner_indices.y]
                                                , corner_values [corner_indices.x]
                                                , corner_values [corner_indices.y]
                                                );
        edge_bit <<= 1;
    }

    const int *triangle_indices = cube_index_2_triangle_indices [cube_index];
    for (int i = 0; triangle_indices [i] != -1; ++i) {
        Vertex vertex;
        vertex.position = edge_vertices [triangle_indices [i]];
        vertex.normal = estimate_normal (scene, vertex.position);
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
            process_leaf_node (leaves [i], current_thread_mesh, settings.iso_level, scene);
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
