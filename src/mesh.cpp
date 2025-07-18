#include "mesh.hpp"

Mesh::Mesh() {
    this->vertices = {
        {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}}
    };

    this->indices = {
        0, 1, 2
    };
}

