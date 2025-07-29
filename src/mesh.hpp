#pragma once

#include <vector>

#include "LiteMath.h"

struct Vertex {
    LiteMath::float3 position;
    LiteMath::float3 color;
    LiteMath::float3 normal;
};

class Mesh {
public:
    Mesh();
    Mesh(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs);

    const std::vector<Vertex>& get_vertices() const { return this->vertices; }
    const std::vector<uint32_t>& get_indices() const { return this->indices; }

    void set_data(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs);
    void clear();
    bool is_empty() const { return vertices.empty(); }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

