#include <fstream>
#include <iostream>

#include "vk_buffers.h"
#include "mesh.hpp"

namespace sdf_raster {

Mesh::Mesh() {
}

Mesh::Mesh(std::vector<uint32_t>&& idxs, std::vector<Vertex>&& verts)
    : indices(std::move(idxs)) ,vertices(std::move(verts)) {}

void Mesh::set_data(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs) {
    vertices = std::move(verts);
    indices = std::move(idxs);
}

void Mesh::clear() {
    vertices.clear();
    indices.clear();
}

void Mesh::add_vertex_fast(Vertex v) {
    int new_index = vertices.size();
    this->indices.push_back(new_index);
    this->vertices.push_back(v);
}

void Mesh::add_triangle(Vertex a, Vertex b, Vertex c) {
    this->add_vertex_fast (a);
    this->add_vertex_fast (b);
    this->add_vertex_fast (c);
}

void save_mesh_as_obj (const Mesh& mesh, const std::string& filename) {
    printf ("Saving mesh to '%s'...\n", filename.c_str ());

    std::ofstream out (filename);
    if (!out) {
        throw std::runtime_error ("Failed to open file: " + filename);
    }

    const auto& vertices = mesh.get_vertices ();
    const auto& indices  = mesh.get_indices ();

    for (const auto& v : vertices) {
        out << "v " 
            << v.position.x << " " << v.position.y << " " << v.position.z 
            << "\n";
    }
    for (const auto& v : vertices) {
        out << "vn " 
            << v.normal.x << " " << v.normal.y << " " << v.normal.z 
            << "\n";
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0] + 1;
        uint32_t i1 = indices[i + 1] + 1;
        uint32_t i2 = indices[i + 2] + 1;
        out << "f "
            << i0 << "//" << i0 << " "
            << i1 << "//" << i1 << " "
            << i2 << "//" << i2 << "\n";
    }
    printf ("Saved mesh to '%s'.\n", filename.c_str ());
}

MeshDescriptorSetInfo create_mesh_descriptor_set (
    VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , vk_utils::DescriptorMaker& ds_maker
    , VkShaderStageFlags shader_stage_flags
    , size_t max_vertices_count
    , size_t max_frames_in_flight) {
    std::cout << "create_mesh_descriptor_set: creating..." << std::endl;
    MeshDescriptorSetInfo info = {};

    if (!copy_helper) {
        throw std::runtime_error ("ICopyEngine shared_ptr cannot be null.");
    }

    // VkDeviceSize vertices_size = max_vertices_count * sizeof (Vertex);
    // VkDeviceSize indices_size = max_vertices_count * sizeof (LiteMath::uint) * 3;
    VkDeviceSize indices_size = 100000 * sizeof (uint32_t);
    VkDeviceSize vertices_size = 100000 * sizeof (Vertex);

    // if (max_vertices_count == 0) {
    //     throw std::runtime_error ("create_mesh_descriptor_set: max_vertices_count is 0, cannot create descriptor set.");
    // }
    // std::cout << "create_mesh_descriptor_set: max_vertices_count = " << max_vertices_count << std::endl;

    std::vector <VkBuffer> buffers (1 + 2 * max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (1 + 2 * max_frames_in_flight);

    info.vertices_buffers.clear ();
    info.indices_buffers.clear ();
    for (int i = 0; i < max_frames_in_flight; ++i) {
        buffers [i * 2 + 0] = vk_utils::createBuffer (device, vertices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT, &mem_reqs [i * 2 + 0]);
        buffers [i * 2 + 1] = vk_utils::createBuffer (device, indices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT, &mem_reqs [i * 2 + 1]);
        info.vertices_buffers.push_back (buffers [i * 2 + 0]);
        info.indices_buffers.push_back (buffers [i * 2 + 1]);
    }

    buffers [2 * max_frames_in_flight] = vk_utils::createBuffer (device, sizeof (LiteMath::uint)
            , VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2 * max_frames_in_flight]);
    info.insufficent_mem_flag_buffer = buffers [2 * max_frames_in_flight];

    info.memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    LiteMath::uint insufficent_mem_flag = 0;
    copy_helper->UpdateBuffer (info.insufficent_mem_flag_buffer, 0, &insufficent_mem_flag, sizeof (LiteMath::uint));

    info.descriptor_sets.resize (max_frames_in_flight);
    for (int i = 0; i < max_frames_in_flight; ++i) {
        ds_maker.BindBegin (shader_stage_flags);
        ds_maker.BindBuffer (0, info.vertices_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (1, info.indices_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindBuffer (2, info.insufficent_mem_flag_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        ds_maker.BindEnd (&info.descriptor_sets [i], &info.descriptor_set_layout);
    }

    return info;
}

void cleanup_mesh_descriptor_set (VkDevice device, MeshDescriptorSetInfo& info) {
    for (int i = 0; i < info.vertices_buffers.size (); ++i) {
        if (info.vertices_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.vertices_buffers [i], nullptr);
            info.vertices_buffers [i] = VK_NULL_HANDLE;
        }
    }

    for (int i = 0; i < info.indices_buffers.size (); ++i) {
        if (info.indices_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, info.indices_buffers [i], nullptr);
            info.indices_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (info.insufficent_mem_flag_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, info.insufficent_mem_flag_buffer, nullptr);
        info.insufficent_mem_flag_buffer = VK_NULL_HANDLE;
    }

    if (info.memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, info.memory, nullptr);
        info.memory = VK_NULL_HANDLE;
    }

    info = {};
}

LiteMath::uint fetch_insufficent_mem_flag (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, MeshDescriptorSetInfo info) {
    LiteMath::uint data;
    copy_helper->ReadBuffer (info.insufficent_mem_flag_buffer, 0, &data, sizeof (LiteMath::uint));
    return data;
}

Mesh fetch_mesh_from_device (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, const MeshDescriptorSetInfo& info, size_t frame) {
    std::vector <uint32_t> idxs (100000);
    std::vector <Vertex> verts (100000);

    copy_helper->ReadBuffer (info.indices_buffers [frame], 0, idxs.data (), sizeof (uint32_t) * 100000);
    copy_helper->ReadBuffer (info.vertices_buffers [frame], 0, verts.data (), sizeof (Vertex) * 100000);

    return {std::move (idxs), std::move (verts)};
}

}

