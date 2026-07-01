// data/mesh.hpp
#pragma once

#include "resources/model_resource.hpp"

#include "shader_common.hpp"

#include <LiteMath.h>
#include <vk_copy.h>
#include <vk_descriptor_sets.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sdf_raster {

// TODO: move to scenes/obj
class Mesh {
public:
    Mesh();
    Mesh(std::vector<uint32_t>&& idxs, std::vector<Vertex>&& verts);

    const std::vector<Vertex>& get_vertices() const { return this->vertices; }
    const std::vector<uint32_t>& get_indices() const { return this->indices; }

    void set_data(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs);
    void clear();
    bool is_empty() const { return vertices.empty(); }

    void add_vertex(Vertex v);
    void add_vertex_fast(Vertex v);
    void add_triangle(Vertex a, Vertex b, Vertex c);

private:
    uint32_t index_vertex(const Vertex& v);

private:
    std::vector<uint32_t> indices {};
    std::vector<Vertex> vertices {};
};

void save_mesh_as_obj (const Mesh& mesh, const std::string& filename);

class MeshDescriptorSetInfo : public ModelResource {
public:
    MeshDescriptorSetInfo (VkDevice device
        , VkPhysicalDevice physical_device
        , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
        , VkShaderStageFlags shader_stage_flags
        , size_t vertices_count
        , size_t indices_count
        , size_t max_frames_in_flight);
    ~MeshDescriptorSetInfo ();

    VkDescriptorSet get_descriptor_set (uint32_t fif_index) const {
        return (fif_index + 1 > this->descriptor_sets.size ()) ? VK_NULL_HANDLE : this->descriptor_sets [fif_index];
    }

    VkDescriptorSetLayout get_layout () const { return this->descriptor_set_layout; }

    VkBuffer get_vertex_buffer (size_t fif_index) {
        return (fif_index + 1 > this->vertices_buffers.size ()) ? this->vertices_buffers [0] : this->vertices_buffers [fif_index];
    }

    VkBuffer get_index_buffer (size_t fif_index) {
        return (fif_index + 1 > this->indices_buffers.size ()) ? this->indices_buffers [0] : this->indices_buffers [fif_index];
    }

    size_t get_indices_count () { return indices_count; }

    LiteMath::uint fetch_insufficent_mem_flag ();
    Mesh fetch_mesh_from_device (size_t fif_index);

private:
    VkDevice device = VK_NULL_HANDLE;

    std::shared_ptr <vk_utils::ICopyEngine> copy_helper;

    std::unique_ptr <vk_utils::DescriptorMaker> desc_maker; 
    std::vector <VkDescriptorSet> descriptor_sets;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

    std::vector <VkBuffer> vertices_buffers;
    std::vector <VkBuffer> indices_buffers;
    VkBuffer insufficent_mem_flag_buffer = VK_NULL_HANDLE;

    size_t vertices_count;
    size_t indices_count;

    VkDeviceMemory memory = VK_NULL_HANDLE;
};

}

