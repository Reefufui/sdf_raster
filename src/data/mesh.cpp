// data/mesh.cpp
#include <fstream>

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
    uint32_t new_index = static_cast <uint32_t> (vertices.size ());
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

void save_mesh_as_obj_append (const Mesh& mesh, const std::string& filename, uint32_t vertex_offset) {
    std::ofstream out (filename, std::ios::app);
    if (!out) {
        throw std::runtime_error ("Failed to open file for append: " + filename);
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
        uint32_t i0 = indices[i + 0] + 1 + vertex_offset;
        uint32_t i1 = indices[i + 1] + 1 + vertex_offset;
        uint32_t i2 = indices[i + 2] + 1 + vertex_offset;
        out << "f "
            << i0 << "//" << i0 << " "
            << i1 << "//" << i1 << " "
            << i2 << "//" << i2 << "\n";
    }
}

MeshDescriptorSetInfo::MeshDescriptorSetInfo (VkDevice device
    , VkPhysicalDevice physical_device
    , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
    , VkShaderStageFlags shader_stage_flags
    , size_t vertices_count
    , size_t indices_count
    , size_t max_frames_in_flight) : device (device), copy_helper (copy_helper), vertices_count (vertices_count), indices_count (indices_count) {
    if (!copy_helper) {
        throw std::runtime_error ("MeshDescriptorSetInfo: ICopyEngine shared_ptr cannot be null.");
    }

    VkDeviceSize vertices_size = vertices_count * sizeof (Vertex);
    VkDeviceSize indices_size = indices_count * sizeof (uint32_t);

    if (vertices_size == 0 || indices_size == 0) {
        throw std::runtime_error ("MeshDescriptorSetInfo: vertices or indices count is 0, cannot create empty Vulkan resources.");
    }

    std::vector <VkBuffer> buffers (1 + 2 * max_frames_in_flight);
    std::vector <VkMemoryRequirements> mem_reqs (1 + 2 * max_frames_in_flight);

    this->vertices_buffers.clear ();
    this->indices_buffers.clear ();
    for (size_t i = 0; i < max_frames_in_flight; ++i) {
        buffers [i * 2 + 0] = vk_utils::createBuffer (device, vertices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT, &mem_reqs [i * 2 + 0]);
        buffers [i * 2 + 1] = vk_utils::createBuffer (device, indices_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT, &mem_reqs [i * 2 + 1]);
        this->vertices_buffers.push_back (buffers [i * 2 + 0]);
        this->indices_buffers.push_back (buffers [i * 2 + 1]);
    }

    buffers [2 * max_frames_in_flight] = vk_utils::createBuffer (device, sizeof (LiteMath::uint)
            , VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &mem_reqs [2 * max_frames_in_flight]);
    this->insufficent_mem_flag_buffer = buffers [2 * max_frames_in_flight];

    this->memory = vk_utils::allocateAndBindWithPadding (device, physical_device, buffers);

    LiteMath::uint insufficent_mem_flag = 0;
    copy_helper->UpdateBuffer (this->insufficent_mem_flag_buffer, 0, &insufficent_mem_flag, sizeof (LiteMath::uint));

    if (shader_stage_flags) {
        vk_utils::DescriptorTypesVec pool_sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * max_frames_in_flight }
        };
        this->desc_maker = std::make_unique <vk_utils::DescriptorMaker> (device, pool_sizes, max_frames_in_flight);

        this->descriptor_sets.resize (max_frames_in_flight);
        for (size_t i = 0; i < max_frames_in_flight; ++i) {
            this->desc_maker->BindBegin (shader_stage_flags);
            this->desc_maker->BindBuffer (0, this->vertices_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            this->desc_maker->BindBuffer (1, this->indices_buffers [i], VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            this->desc_maker->BindBuffer (2, this->insufficent_mem_flag_buffer, VK_NULL_HANDLE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            this->desc_maker->BindEnd (&this->descriptor_sets [i], &this->descriptor_set_layout);
        }
    }
}

MeshDescriptorSetInfo::~MeshDescriptorSetInfo () {
    if (this->device == VK_NULL_HANDLE) return;

    this->desc_maker.reset ();

    for (size_t i = 0; i < this->vertices_buffers.size (); ++i) {
        if (this->vertices_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->vertices_buffers [i], nullptr);
            this->vertices_buffers [i] = VK_NULL_HANDLE;
        }
    }

    for (size_t i = 0; i < this->indices_buffers.size (); ++i) {
        if (this->indices_buffers [i] != VK_NULL_HANDLE) {
            vkDestroyBuffer (device, this->indices_buffers [i], nullptr);
            this->indices_buffers [i] = VK_NULL_HANDLE;
        }
    }

    if (this->insufficent_mem_flag_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer (device, this->insufficent_mem_flag_buffer, nullptr);
        this->insufficent_mem_flag_buffer = VK_NULL_HANDLE;
    }

    if (this->memory != VK_NULL_HANDLE) {
        vkFreeMemory (device, this->memory, nullptr);
        this->memory = VK_NULL_HANDLE;
    }
}

LiteMath::uint MeshDescriptorSetInfo::fetch_insufficent_mem_flag () {
    LiteMath::uint data;
    this->copy_helper->ReadBuffer (this->insufficent_mem_flag_buffer, 0, &data, sizeof (LiteMath::uint));
    return data;
}

Mesh MeshDescriptorSetInfo::fetch_mesh_from_device (size_t fif_index) {
    std::vector <uint32_t> idxs (this->indices_count);
    std::vector <Vertex> verts (this->vertices_count);

    this->copy_helper->ReadBuffer (this->indices_buffers [fif_index], 0, idxs.data (), sizeof (uint32_t) * this->indices_count);
    this->copy_helper->ReadBuffer (this->vertices_buffers [fif_index], 0, verts.data (), sizeof (Vertex) * this->vertices_count);

    return {std::move (idxs), std::move (verts)};
}

}

