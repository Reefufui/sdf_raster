#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

#include "vk_buffers.h"

#include "logger.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

// TODO: move to scenes/octree
void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump) {
    std::ofstream dump_file (path_to_dump);
    if (!dump_file.is_open()) {
        LOG_ERROR ("could not open file {} for dumping octree", path_to_dump);
        return;
    }

    dump_file << "SDF Octree Dump:" << std::endl;
    dump_file << "Total nodes: " << scene.nodes.size() << std::endl;
    dump_file << "----------------------------------------" << std::endl;

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& node = scene.nodes[i];
        dump_file << "Node [" << i << "]:" << std::endl;
        dump_file << "  Values: [";
        for (int j = 0; j < 8; ++j) {
            dump_file << node.values[j] << (j < 7 ? ", " : "");
        }
        dump_file << "]" << std::endl;
        dump_file << "  Offset: " << node.offset << std::endl;
        if (node.offset == 0) {
            dump_file << "  Type: Leaf Node" << std::endl;
        } else {
            dump_file << "  Type: Internal Node (children start around index after offset manipulation, or at offset index)" << std::endl;
        }
        dump_file << "----------------------------------------" << std::endl;
    }

    dump_file.close();
    LOG_INFO ("SDF Octree successfully dumped to '{}'", path_to_dump);
}

// TODO: move to scenes/octree
float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p) {
    const SdfOctreeNode* node = &scene.nodes [0];
    LiteMath::float3 min_corner = {-1.0f, -1.0f, -1.0f};
    float voxel_size = 2.0f;

    while (node->offset != 0) {
        float half = voxel_size * 0.5f;
        unsigned child_index = 0;
        if (p.x >= min_corner.x + half) child_index |= 1, min_corner.x += half;
        if (p.y >= min_corner.y + half) child_index |= 2, min_corner.y += half;
        if (p.z >= min_corner.z + half) child_index |= 4, min_corner.z += half;
        voxel_size = half;
        node = &scene.nodes [node->offset + child_index];
    }

    LiteMath::float3 local = (p - min_corner) / voxel_size;
    auto lerp = [] (float a, float b, float t) { return a + t * (b - a); };

    float c00 = lerp (node->values [0], node->values [1], local.x);
    float c01 = lerp (node->values [4], node->values [5], local.x);
    float c10 = lerp (node->values [3], node->values [2], local.x);
    float c11 = lerp (node->values [7], node->values [6], local.x);

    float c0 = lerp (c00, c10, local.y);
    float c1 = lerp (c01, c11, local.y);

    return lerp (c0, c1, local.z);
}

SdfOctreeDescriptorSetInfo::SdfOctreeDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , const sdf_raster::SdfOctree& octree
        , const size_t subtree_root_level
        , size_t max_frames_in_flight) : device (device) {
    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    VkDeviceSize octree_nodes_size = octree.nodes.size () * sizeof (SdfOctreeNode);
    VkDeviceSize subtree_size = (1LL << (3 * subtree_root_level)) * sizeof (NodeContext); // NOTE: max octree nodes on level: pow (8, level)

    if (octree_nodes_size == 0) {
        throw std::runtime_error ("SdfOctree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (1 + max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (1 + max_frames_in_flight);

    this->subtree_root_buffers.clear ();

    buffers [0] = vk_utils::createBuffer (device, octree_nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    this->nodes_buffer = buffers [0];

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i + 1] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i + 1]);
        this->subtree_root_buffers.push_back (buffers [i + 1]);
    }

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (this->nodes_buffer, 0, octree.nodes.data (), octree_nodes_size);

    vk_utils::DescriptorTypesVec pool_sizes = {
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * max_frames_in_flight }
    };
    this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

    this->descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        this->desc_maker->BindBegin (shader_stage_flags);
        this->desc_maker->BindBuffer (0, this->nodes_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindBuffer (1, this->subtree_root_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
    }
}

SdfOctreeDescriptorSetInfo::~SdfOctreeDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    if (this->nodes_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, this->nodes_buffer, nullptr);
        this->nodes_buffer = VK_NULL_HANDLE;
    }

    for (size_t i = 0; i < this->subtree_root_buffers.size (); ++i) {
        if (this->subtree_root_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->subtree_root_buffers [i], nullptr);
            this->subtree_root_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

int calc_cube_index (const float arr [8]) {
    int cube_index = 0;

    for (int i = 0; i < 8; ++i) {
        if (arr [i] < 0.0f) {
            cube_index |= (1 << i);
        }
    }

    return cube_index;
};

namespace {

struct StackFrame {
    uint32_t node_idx;
    LiteMath::float3 min_corner;
    float voxel_size;
    int level;
};

}

std::vector <NodeContext> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend) {
    std::vector <NodeContext> payloads;

    if (scene.nodes.empty ()) {
        return payloads;
    }

    std::stack <StackFrame> s;

    LiteMath::float3 root_min_corner = {-1.0f, -1.0f, -1.0f};
    float root_voxel_size = 2.0f;
    uint32_t root_node_idx = 0;

    s.push ({root_node_idx, root_min_corner, root_voxel_size, 0});

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        const SdfOctreeNode& node = scene.nodes [current.node_idx];

        if (current.level >= max_level_to_descend || node.offset == 0) {
            int cube_index = calc_cube_index (node.values);
            if (node.offset == 0 && (cube_index == 0 || cube_index == 255)) {
                continue; // no triangles
            }
            payloads.push_back ({
                current.min_corner.x,
                current.min_corner.y,
                current.min_corner.z,
                current.voxel_size,
                static_cast <int> (current.node_idx),
                cube_index
            });
            continue;
        }

        float half = current.voxel_size * 0.5f;
        for (int i = 7; i >= 0; --i) {
            LiteMath::float3 child_min_corner = current.min_corner;

            if ((i & 1) != 0) child_min_corner.x += half;
            if ((i & 2) != 0) child_min_corner.y += half;
            if ((i & 4) != 0) child_min_corner.z += half;

            uint32_t child_node_idx = node.offset + i;

            s.push ({
                child_node_idx,
                child_min_corner,
                half,
                current.level + 1
            });
        }
    }

    return payloads;
}

std::vector <NodeContext> process_subtree (const SdfOctree& scene, StackFrame initial_frame, int max_level_to_descend) {
    std::vector <NodeContext> local_payloads;
    std::stack <StackFrame> s;
    s.push (initial_frame);

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        const SdfOctreeNode& node = scene.nodes [current.node_idx];

        if (current.level >= max_level_to_descend || node.offset == 0) {
            int cube_index = calc_cube_index (node.values);
            if (node.offset == 0 && (cube_index == 0 || cube_index == 255)) {
                continue; // NOTE: cube_index == 255 may be useful as best occluders
            }
            local_payloads.push_back ({current.min_corner.x
                , current.min_corner.y
                , current.min_corner.z
                , current.voxel_size
                , static_cast <int> (current.node_idx)
                , cube_index
            });
            continue;
        }

        float half = current.voxel_size * 0.5f;
        for (int i = 7; i >= 0; --i) {
            LiteMath::float3 child_min_corner = current.min_corner;
            if ((i & 1) != 0) child_min_corner.x += half;
            if ((i & 2) != 0) child_min_corner.y += half;
            if ((i & 4) != 0) child_min_corner.z += half;
            uint32_t child_node_idx = node.offset + i;

            s.push ({child_node_idx, child_min_corner, half, current.level + 1});
        }
    }

    return local_payloads;
}

std::vector <NodeContext> get_octree_subtrees_payloads_parallel (const SdfOctree& scene, int max_level_to_descend) {
    if (scene.nodes.empty ()) {
        return {};
    }

    unsigned int num_threads = std::thread::hardware_concurrency ();
    int level_to_split = (num_threads > 1) ? static_cast <int> (ceil (log (4 * num_threads) / log(8))) : 0;
    if (level_to_split <= 0) level_to_split = 1;
    level_to_split = std::min (level_to_split, max_level_to_descend);


    std::vector <StackFrame> tasks;
    std::stack <StackFrame> s;

    s.push ({0, {-1.0f, -1.0f, -1.0f}, 2.0f, 0});

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        if (current.level >= level_to_split || scene.nodes [current.node_idx].offset == 0) {
            tasks.push_back (current);
            continue;
        }

        const SdfOctreeNode& node = scene.nodes [current.node_idx];
        float half = current.voxel_size * 0.5f;

        for (int i = 7; i >= 0; --i) {
            LiteMath::float3 child_min_corner = current.min_corner;
            if ((i & 1) != 0) child_min_corner.x += half;
            if ((i & 2) != 0) child_min_corner.y += half;
            if ((i & 4) != 0) child_min_corner.z += half;
            uint32_t child_node_idx = node.offset + i;

            s.push ({child_node_idx, child_min_corner, half, current.level + 1});
        }
    }

    if (tasks.size () <= 1) {
        return process_subtree (scene, tasks.empty () ? StackFrame {0, {-1.0f, -1.0f, -1.0f}, 2.0f, 0} : tasks [0], max_level_to_descend);
    }

    std::vector <std::future <std::vector <NodeContext>>> futures;

    size_t tasks_per_thread = (tasks.size () + num_threads - 1) / num_threads;
    for (size_t i = 0; i < tasks.size (); i += tasks_per_thread) {
        auto start = tasks.begin () + i;
        auto end = tasks.begin () + std::min (i + tasks_per_thread, tasks.size ());
        std::vector <StackFrame> thread_tasks (start, end);

        futures.push_back (std::async (std::launch::async, [thread_tasks, &scene, max_level_to_descend] {
            std::vector <NodeContext> thread_payloads;
            for (const auto& task : thread_tasks) {
                auto partial_result = process_subtree (scene, task, max_level_to_descend);
                if (!partial_result.empty ()) {
                    thread_payloads.insert (thread_payloads.end (), partial_result.begin (), partial_result.end ());
                }
            }
            return thread_payloads;
        }));
    }

    std::vector <NodeContext> final_payloads;

    std::vector <std::vector <NodeContext>> all_results;
    all_results.reserve (futures.size ());
    size_t total_size = 0;

    for (auto& f : futures) {
        all_results.push_back (f.get ());
        total_size += all_results.back ().size ();
    }

    final_payloads.reserve (total_size);

    for (const auto& res : all_results) {
        final_payloads.insert (final_payloads.end (), res.begin (), res.end ());
    }

    return final_payloads;
}

int get_octree_max_depth (const SdfOctree& scene) {
    if (scene.nodes.empty ()) {
        return -1;
    }

    int max_overall_depth = 0;

    struct StackFrame {
        uint32_t node_idx;
        int level;
    };
    std::stack <StackFrame> s;

    uint32_t root_node_idx = 0;
    s.push ({root_node_idx, 0});

    while (!s.empty ()) {
        StackFrame current = s.top ();
        s.pop ();

        max_overall_depth = std::max (max_overall_depth, current.level);

        const SdfOctreeNode& node = scene.nodes [current.node_idx];

        if (node.offset == 0) {
            continue;
        }

        for (int i = 0; i < 8; ++i) {
            uint32_t child_node_idx = node.offset + i;
            s.push ({ child_node_idx, current.level + 1 });
        }
    }

    return max_overall_depth;
}

}

