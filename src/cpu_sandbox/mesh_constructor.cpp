#include <iostream>
#include <fstream>

#include "cpu_sandbox.h"
#include "marching_cubes_lookup_table.hpp"

namespace cpu_sandbox {

class MeshSingleton {
public:
    static MeshSingleton& instance () {
        static MeshSingleton instance;
        return instance;
    }

    void add_vertex (float4 position) {
        this->m_vertices.push_back (position);
    }

    void add_triangle (uint3 indices) {
        this->m_triangles.push_back (indices);
    }

    size_t get_vertex_count () {
        return this->m_vertices.size ();
    }

    void dump_obj (const std::string& filename) {
        std::ofstream outFile (filename);
        if (!outFile.is_open ()) {
            std::cerr << "Error: Could not open file " << filename << std::endl;
            return;
        }

        for (const auto& v : this->m_vertices) {
            outFile << "v " << v.x << " " << v.y << " " << v.z << std::endl;
        }

        for (const auto& t : this->m_triangles) {
            outFile << "f " << t.x + 1 << " " << t.y + 1 << " " << t.z + 1 << std::endl;
        }

        outFile.close ();
        std::cout << "Mesh dumped to " << filename << std::endl;
    }

private:
    MeshSingleton () {}
    MeshSingleton (const MeshSingleton&) = delete;
    MeshSingleton& operator= (const MeshSingleton&) = delete;

    std::vector <float4> m_vertices;
    std::vector <uint3> m_triangles;
};

void add_vertex (float4 position) {
    MeshSingleton::instance ().add_vertex (position);
}

void add_triangle (uint3 indices) {
    MeshSingleton::instance ().add_triangle (indices);
}

size_t get_vertex_count () {
    return MeshSingleton::instance ().get_vertex_count ();
}

void dump_obj (const std::string& filename) {
    MeshSingleton::instance ().dump_obj (filename);
}

}

