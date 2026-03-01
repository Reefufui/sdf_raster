#include <cstdint>
#include <fstream>
#include <stack>
#include <future>

#include "vk_buffers.h"

#include "logger.hpp"
#include "sdf_octree.hpp"

namespace sdf_raster {

void load_sdf_octree (SdfOctree& scene, const std::filesystem::path& path) {
    std::ifstream fs (path, std::ios::binary);
    unsigned sz = 0;
    fs.read ((char *) &sz, sizeof (unsigned));
    scene.nodes.resize (sz);
    fs.read ((char *) scene.nodes.data (), scene.nodes.size () * sizeof (SdfOctreeNode));
    fs.close ();
    scene.name = path.stem ().string ();
}

void save_sdf_octree (const SdfOctree &scene, const std::string &path) {
    std::ofstream fs (path, std::ios::binary);
    size_t size = scene.nodes.size ();
    fs.write ((const char *) &size, sizeof (unsigned));
    fs.write ((const char *) scene.nodes.data (), size * sizeof (SdfOctreeNode));
    fs.flush ();
    fs.close ();
}

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

SdfOctreeDescriptorSetInfo create_sdf_octree_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const sdf_raster::SdfOctree& octree
        , const std::vector <NodeContext>& subtrees) {
    SdfOctreeDescriptorSetInfo info = {};

    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    VkDeviceSize octree_nodes_size = octree.nodes.size () * sizeof (SdfOctreeNode);
    VkDeviceSize subtree_size = subtrees.size () * sizeof (NodeContext);

    if (octree_nodes_size == 0) {
        throw std::runtime_error ("SdfOctree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (2);
    std::vector <VkMemoryRequirements> mem_reqs (2);

    buffers [0] = vk_utils::createBuffer (device, octree_nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    buffers [1] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [1]);
    info.nodes_buffer = buffers [0];
    info.subtree_buffer = buffers [1];

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (info.nodes_buffer, 0, octree.nodes.data (), octree_nodes_size);
    copy_helper->UpdateBuffer (info.subtree_buffer, 0, subtrees.data (), subtree_size);

    ds_maker.BindBegin (shader_stage_flags);
    ds_maker.BindBuffer (0, info.nodes_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindBuffer (1, info.subtree_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindEnd (&info.descriptor_set, &info.descriptor_set_layout);

    return info;
}

ActiveLeafsDescriptorSetInfo create_active_leafs_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , size_t active_leafs_count
        , size_t max_frames_in_flight) {
    ActiveLeafsDescriptorSetInfo info = {};

    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    const VkDeviceSize active_leafs_size = active_leafs_count * sizeof (NodeContext);
    const VkDeviceSize active_leaf_counter_size = sizeof (VkDispatchIndirectCommand);
    const VkDeviceSize active_leaf_vertices_count_size = active_leafs_count * sizeof (uint);
    const VkDeviceSize active_leaf_indices_count_size = active_leafs_count * sizeof (uint);

    std::vector <VkBuffer> buffers (max_frames_in_flight * 4);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight * 4);

    info.active_leafs_buffers.clear ();
    info.active_leaf_counter_buffers.clear ();
    info.active_leaf_vertices_count_buffers.clear ();
    info.active_leaf_indices_count_buffers.clear ();

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i * 4 + 0] = vk_utils::createBuffer (device, active_leafs_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 4 + 0]);
        buffers [i * 4 + 1] = vk_utils::createBuffer (device, active_leaf_counter_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 4 + 1]);
        buffers [i * 4 + 2] = vk_utils::createBuffer (device, active_leaf_vertices_count_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 4 + 2]);
        buffers [i * 4 + 3] = vk_utils::createBuffer (device, active_leaf_indices_count_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i * 4 + 3]);

        info.active_leafs_buffers.push_back (buffers [i * 4 + 0]);
        info.active_leaf_counter_buffers.push_back (buffers [i * 4 + 1]);
        info.active_leaf_vertices_count_buffers.push_back (buffers [i * 4 + 2]);
        info.active_leaf_indices_count_buffers.push_back (buffers [i * 4 + 3]);
    }

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.active_leafs_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (1, info.active_leaf_counter_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (2, info.active_leaf_vertices_count_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (3, info.active_leaf_indices_count_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

DrawIndexedIndirectCommandDescriptorSetInfo create_draw_indexed_indirect_command_descriptor_set (
        VkDevice device
        , VkPhysicalDevice physical_device
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , size_t max_frames_in_flight) {
    DrawIndexedIndirectCommandDescriptorSetInfo info = {};

    const VkDeviceSize draw_indexed_indirect_command_size = sizeof (VkDrawIndexedIndirectCommand);

    std::vector <VkBuffer> buffers (max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (max_frames_in_flight);

    info.draw_indexed_indirect_command_buffers.clear ();

    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i] = vk_utils::createBuffer (device, draw_indexed_indirect_command_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [i]);

        info.draw_indexed_indirect_command_buffers.push_back (buffers [i]);
    }

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    info.descriptor_sets.resize (max_frames_in_flight);
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.draw_indexed_indirect_command_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_sdf_octree_descriptor_set (VkDevice device, SdfOctreeDescriptorSetInfo& info) {
    if (info.nodes_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.nodes_buffer, nullptr);
        info.nodes_buffer = VK_NULL_HANDLE;
    }

    if (info.subtree_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.subtree_buffer, nullptr);
        info.subtree_buffer = VK_NULL_HANDLE;
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

void cleanup_active_leafs_descriptor_set (VkDevice device, ActiveLeafsDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.active_leafs_buffers.size (); ++i) {
        if (info.active_leafs_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leafs_buffers [i], nullptr);
            info.active_leafs_buffers [i] = VK_NULL_HANDLE;
        }
        if (info.active_leaf_counter_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leaf_counter_buffers [i], nullptr);
            info.active_leaf_counter_buffers [i] = VK_NULL_HANDLE;
        }
        if (info.active_leaf_vertices_count_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leaf_vertices_count_buffers [i], nullptr);
            info.active_leaf_vertices_count_buffers [i] = VK_NULL_HANDLE;
        }
        if (info.active_leaf_indices_count_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.active_leaf_indices_count_buffers [i], nullptr);
            info.active_leaf_indices_count_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

void cleanup_draw_indexed_indirect_command_descriptor_set (VkDevice device, DrawIndexedIndirectCommandDescriptorSetInfo& info) {
    for (size_t i = 0; i < info.draw_indexed_indirect_command_buffers.size (); ++i) {
        if (info.draw_indexed_indirect_command_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.draw_indexed_indirect_command_buffers [i], nullptr);
            info.draw_indexed_indirect_command_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
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

std::vector <NodeContext> fetch_active_leafs (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame) {
    std::vector <NodeContext> active_leafs_cpu (active_leafs_count);
    copy_helper->ReadBuffer (info.active_leafs_buffers [frame], 0, active_leafs_cpu.data (), active_leafs_count * sizeof (NodeContext));
    return active_leafs_cpu;
}

uint32_t fetch_active_leaf_counter (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t frame) {
    VkDispatchIndirectCommand command = {0, 0, 0};
    copy_helper->ReadBuffer (info.active_leaf_counter_buffers [frame], 0, &command, sizeof (VkDispatchIndirectCommand));
    return static_cast <uint32_t> (command.x);
}

std::vector <uint> fetch_vertices_count (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame) {
    std::vector <uint> vertices_count (active_leafs_count);
    copy_helper->ReadBuffer (info.active_leaf_vertices_count_buffers [frame], 0, vertices_count.data (), active_leafs_count * sizeof (uint));
    return vertices_count;
}

std::vector <uint> fetch_indices_count (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, ActiveLeafsDescriptorSetInfo info, size_t active_leafs_count, size_t frame) {
    std::vector <uint> indices_count (active_leafs_count);
    copy_helper->ReadBuffer (info.active_leaf_indices_count_buffers [frame], 0, indices_count.data (), active_leafs_count * sizeof (uint));
    return indices_count;
}

}

