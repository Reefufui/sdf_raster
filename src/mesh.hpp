#pragma once

#include <unordered_map>
#include <vector>

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
    
}

