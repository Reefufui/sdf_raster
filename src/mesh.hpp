#pragma once

#include <vector>

#include "LiteMath.h"

struct Vertex {
    LiteMath::float3 position;
    LiteMath::float3 color;
};

class Mesh {
public:
    Mesh();
    const std::vector<Vertex>& get_vertices() const { return this->vertices; }
    const std::vector<uint32_t>& get_indices() const { return this->indices; }

private:
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

