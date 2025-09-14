#include "mesh.hpp"

namespace sdf_raster {

std::size_t hash_vec(const LiteMath::float3& v) {
    float x = v.x;
    float y = v.y;
    float z = v.z;

    x = x >=0 ? 2 * x : -2 * x - 1;
    y = y >=0 ? 2 * y : -2 * y - 1;
    z = z >=0 ? 2 * z : -2 * z - 1;

    auto max = std::max({x,y,z});
    double hash = pow(max,3) + (2 * max * z) + z;
    if(max == z)
        hash += pow(std::max(x,y), 2);
    if(y >= x)
        hash += x+y;
    else
        hash += y;
    return static_cast<std::size_t>(hash);
}

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

void Mesh::add_vertex(Vertex v) {
    uint32_t index = this->index_vertex(v);
    this->indices.push_back(index);
}

void Mesh::add_vertex_fast(Vertex v) {
    int new_index = vertices.size();
    this->indices.push_back(new_index);
    this->vertices.push_back(v);
}

void Mesh::add_triangle(Vertex a, Vertex b, Vertex c) {
    uint32_t index_a = this->index_vertex(a);
    uint32_t index_b = this->index_vertex(b);
    uint32_t index_c = this->index_vertex(c);

    this->indices.push_back(index_a);
    this->indices.push_back(index_b);
    this->indices.push_back(index_c);
}

uint32_t Mesh::index_vertex(const Vertex& v) {
    auto it = vertex_to_index.find(v);
    if (it != vertex_to_index.end()) {
        return it->second;
    } else {
        int new_index = vertices.size();
        vertices.push_back(v);
        vertex_to_index[v] = new_index;
        return new_index;
    }
}

}

