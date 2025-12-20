#include <cstdint>
#include <fstream>
#include <iostream>
#include <stack>

#include "sdf_octree.hpp"
#include "shaders/common.h" // Payload
#include "vk_buffers.h"

namespace sdf_raster {

void load_sdf_octree (SdfOctree &scene, const std::string &path) {
    std::ifstream fs (path, std::ios::binary);
    unsigned sz = 0;
    fs.read ((char *) &sz, sizeof (unsigned));
    scene.nodes.resize (sz);
    fs.read ((char *) scene.nodes.data (), scene.nodes.size () * sizeof (SdfOctreeNode));
    fs.close ();
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
        std::cerr << "Error: Could not open file for dumping: " << path_to_dump << std::endl;
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
            // Если offset != 0, это указывает на начало детей.
            // Мы можем показать, где примерно может начинаться цепочка детей.
            // Важно: это не гарантирует, что именно здесь начинаются *непосредственные* дети,
            // но дает представление о том, куда указывает offset.
            dump_file << "  Type: Internal Node (children start around index after offset manipulation, or at offset index)" << std::endl;
            // Если offset это прямой индекс, то можно вывести:
            // dump_file << "  Children start at index: " << node.offset << std::endl;
            // Однако, учитывая, что offset может быть не просто индексом, а смещением,
            // более корректно будет указать, что это начало группы детских узлов.
        }
        dump_file << "----------------------------------------" << std::endl;
    }

    dump_file.close();
    std::cout << "SDF Octree successfully dumped to: " << path_to_dump << std::endl;
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
        , const sdf_raster::SdfOctree& octree
        , const std::vector <Payload>& subtrees
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , vk_utils::DescriptorMaker& ds_maker
        , VkShaderStageFlags shader_stage_flags
        , const uint32_t frames_count) {
    SdfOctreeDescriptorSetInfo info = {};

    if (!copy_helper) {
        throw std::runtime_error("ICopyEngine shared_ptr cannot be null.");
    }

    VkDeviceSize octree_nodes_size = octree.nodes.size () * sizeof (SdfOctreeNode);
    VkDeviceSize subtree_size = subtrees.size () * sizeof (Payload);
    VkDeviceSize int_stack_size = subtrees.size () * sizeof (int) * MAX_OCTREE_DEPTH * frames_count;
    VkDeviceSize float3_stack_size = subtrees.size () * sizeof (LiteMath::float3) * MAX_OCTREE_DEPTH * frames_count;

    if (octree_nodes_size == 0) {
        throw std::runtime_error ("SdfOctree is empty, cannot create descriptor set.");
    }

    std::vector <VkBuffer> buffers (5);
    std::vector <VkMemoryRequirements> mem_reqs (5);

    buffers [0] = vk_utils::createBuffer (device, octree_nodes_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [0]);
    buffers [1] = vk_utils::createBuffer (device, subtree_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [1]);
    buffers [2] = vk_utils::createBuffer (device, int_stack_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2]);
    buffers [3] = vk_utils::createBuffer (device, float3_stack_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [3]);
    buffers [4] = vk_utils::createBuffer (device, int_stack_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [4]);

    info.nodes_buffer = buffers [0];
    info.subtree_buffer = buffers [1];
    info.node_index_stack_buffer = buffers [2];
    info.coord_stack_buffer = buffers [3];
    info.path_stack_buffer = buffers [4];

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    copy_helper->UpdateBuffer (info.nodes_buffer, 0, octree.nodes.data (), octree_nodes_size);
    copy_helper->UpdateBuffer (info.subtree_buffer, 0, subtrees.data (), subtree_size);

    ds_maker.BindBegin (shader_stage_flags);
    ds_maker.BindBuffer (0, info.nodes_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindBuffer (1, info.subtree_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindBuffer (2, info.node_index_stack_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindBuffer (3, info.coord_stack_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindBuffer (4, info.path_stack_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ds_maker.BindEnd (&info.descriptor_set, &info.descriptor_set_layout);

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

    if (info.node_index_stack_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.node_index_stack_buffer, nullptr);
        info.node_index_stack_buffer = VK_NULL_HANDLE;
    }

    if (info.coord_stack_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.coord_stack_buffer, nullptr);
        info.coord_stack_buffer = VK_NULL_HANDLE;
    }

    if (info.path_stack_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.path_stack_buffer, nullptr);
        info.path_stack_buffer = VK_NULL_HANDLE;
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

std::vector <Payload> get_octree_subtrees_payloads (const SdfOctree& scene, int max_level_to_descend) {
    std::vector <Payload> payloads;

    if (scene.nodes.empty ()) {
        return payloads;
    }

    struct StackFrame {
        uint32_t node_idx;
        LiteMath::float3 min_corner;
        float voxel_size;
        int level;
    };

    std::stack <StackFrame> s;

    LiteMath::float3 root_min_corner = {-1.0f, -1.0f, -1.0f};
    float root_voxel_size = 2.0f;
    uint32_t root_node_idx = 0;

    s.push ({root_node_idx, root_min_corner, root_voxel_size, 0});
    
    const auto calc_cube_index = [] (const float (&arr) [8]) -> int {
        int cube_index = 0;

        for (int i = 0; i < 8; ++i) {
            if (arr [i] < 0.0f) {
                cube_index |= (1 << i);
            }
        }

        return cube_index;
    };

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
                current.min_corner,
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


void dump_octree_subtree_pretty(const SdfOctree& scene, uint32_t subtree_root_node_idx, int max_display_depth, const std::string& prefix, int current_display_depth) {
    if (subtree_root_node_idx >= scene.nodes.size()) {
        std::cerr << prefix << "Error: Node index " << subtree_root_node_idx << " is out of bounds." << std::endl;
        return;
    }

    if (max_display_depth != -1 && current_display_depth > max_display_depth) {
        return;
    }

    const SdfOctreeNode& node = scene.nodes [subtree_root_node_idx];

    std::cout << prefix << (current_display_depth == 0 ? "" : "|-- ")
              << "Node [" << subtree_root_node_idx << "], Display Depth: " << current_display_depth;
    if (node.offset == 0) {
        std::cout << ", Type: Leaf\n";
        for (int i = 0; i < 8; ++i) {
            std::cout << prefix << "|-- |-- "
                << "value [" << i << "] = " << node.values [i] << std::endl;
        }
    } else {
        std::cout << ", Type: Internal (children offset: " << node.offset << ")";
    }
    std::cout << std::endl;

    if (node.offset != 0 && (max_display_depth == -1 || current_display_depth < max_display_depth)) {
        std::string child_prefix = prefix + (current_display_depth == 0 ? "" : "|   ");

        for (int i = 0; i < 8; ++i) {
            uint32_t child_node_idx = node.offset + i;
            
            std::string branch_prefix = child_prefix + (i == 7 ? "    " : "|   ");

            dump_octree_subtree_pretty(
                scene,
                child_node_idx,
                max_display_depth,
                branch_prefix,
                current_display_depth + 1
            );
        }
    }
}

}

