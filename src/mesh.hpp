#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vk_copy.h"
#include "vk_descriptor_sets.h"
#include "LiteMath.h"

namespace sdf_raster {

    struct Vertex {
        LiteMath::float3 position;
        LiteMath::float3 color;
        LiteMath::float3 normal;

        bool operator==(const Vertex& other) const {
            return position.x == other.position.x && position.y == other.position.y && position.z == other.position.z
            && normal.x == other.normal.x && normal.y == other.normal.y && normal.z == other.normal.z
            && color.x == other.color.x && color.y == other.color.y && color.z == other.color.z;
        }
    };

    std::size_t hash_vec(const LiteMath::float3& v);

    struct VertexHash {
        std::size_t operator()(const Vertex& v) const {
            std::size_t seed = 0;
            seed ^= hash_vec(v.position) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash_vec(v.normal) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= hash_vec(v.color) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

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
        std::unordered_map<Vertex, uint32_t, VertexHash> vertex_to_index;
        std::vector<uint32_t> indices {};
        std::vector<Vertex> vertices {};
    };

    void save_mesh_as_obj (const Mesh& mesh, const std::string& filename);

    struct MeshDescriptorSetInfo {
        std::vector <VkDescriptorSet> descriptor_sets;
        VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;

        std::vector <VkBuffer> vertices_buffers;
        std::vector <VkBuffer> indices_buffers;
        VkBuffer indices_count = VK_NULL_HANDLE;
        VkBuffer insufficent_mem_flag_buffer = VK_NULL_HANDLE;

        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    MeshDescriptorSetInfo create_mesh_descriptor_set (
            VkDevice device
            , VkPhysicalDevice physical_device
            , std::shared_ptr <vk_utils::ICopyEngine> copy_helper
            , vk_utils::DescriptorMaker& ds_maker
            , VkShaderStageFlags shader_stage_flags
            , size_t max_vertices_count
            , size_t max_frames_in_flight);

    void cleanup_mesh_descriptor_set (VkDevice device, MeshDescriptorSetInfo& info);

    LiteMath::uint fetch_insufficent_mem_flag (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, MeshDescriptorSetInfo info);

    Mesh fetch_mesh_from_device (std::shared_ptr <vk_utils::ICopyEngine> copy_helper, const MeshDescriptorSetInfo& info, size_t frame);
}

