#include "mesh.hpp"

Mesh::Mesh() {
    this->vertices = {
        {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}
        , {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}
        , {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}}
    };

    this->indices = {
        0, 1, 2
    };
}

Mesh::Mesh(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs)
    : vertices(std::move(verts)), indices(std::move(idxs)) {}

void Mesh::set_data(std::vector<Vertex>&& verts, std::vector<uint32_t>&& idxs) {
    vertices = std::move(verts);
    indices = std::move(idxs);
}

void Mesh::clear() {
    vertices.clear();
    indices.clear();
}

