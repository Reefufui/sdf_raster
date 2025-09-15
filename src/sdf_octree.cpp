#include <cstdint>
#include <fstream>
#include <iostream>

#include "sdf_octree.hpp"

namespace sdf_raster {

void load_sdf_octree (SdfOctree &scene, const std::string &path) {
    std::ifstream fs (path, std::ios::binary);
    unsigned sz = 0;
    fs.read ((char *) &sz, sizeof (unsigned));
    scene.nodes.resize (sz);
    fs.read ((char *) scene.nodes.data (), scene.nodes.size () * sizeof (SdfOctreeNode));
    fs.close ();
}

void save_sdf_octree (const SdfOctree &scene, const std::string &path) {
    std::ofstream fs (path, std::ios::binary);
    size_t size = scene.nodes.size ();
    fs.write ((const char *) &size, sizeof (unsigned));
    fs.write ((const char *) scene.nodes.data (), size * sizeof (SdfOctreeNode));
    fs.flush ();
    fs.close ();
}

void dump_sdf_octree_text (const SdfOctree &scene, const std::string &path_to_dump) {
    std::ofstream dump_file (path_to_dump);
    if (!dump_file.is_open()) {
        std::cerr << "Error: Could not open file for dumping: " << path_to_dump << std::endl;
        return;
    }

    dump_file << "SDF Octree Dump:" << std::endl;
    dump_file << "Total nodes: " << scene.nodes.size() << std::endl;
    dump_file << "----------------------------------------" << std::endl;

    for (size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto& node = scene.nodes[i];
        dump_file << "Node [" << i << "]:" << std::endl;
        dump_file << "  Values: [";
        for (int j = 0; j < 8; ++j) {
            dump_file << node.values[j] << (j < 7 ? ", " : "");
        }
        dump_file << "]" << std::endl;
        dump_file << "  Offset: " << node.offset << std::endl;
        if (node.offset == 0) {
            dump_file << "  Type: Leaf Node" << std::endl;
        } else {
            // Если offset != 0, это указывает на начало детей.
            // Мы можем показать, где примерно может начинаться цепочка детей.
            // Важно: это не гарантирует, что именно здесь начинаются *непосредственные* дети,
            // но дает представление о том, куда указывает offset.
            dump_file << "  Type: Internal Node (children start around index after offset manipulation, or at offset index)" << std::endl;
            // Если offset это прямой индекс, то можно вывести:
            // dump_file << "  Children start at index: " << node.offset << std::endl;
            // Однако, учитывая, что offset может быть не просто индексом, а смещением,
            // более корректно будет указать, что это начало группы детских узлов.
        }
        dump_file << "----------------------------------------" << std::endl;
    }

    dump_file.close();
    std::cout << "SDF Octree successfully dumped to: " << path_to_dump << std::endl;
}

float sample_sdf (const SdfOctree& scene, const LiteMath::float3& p) {
    const SdfOctreeNode* node = &scene.nodes [0];
    LiteMath::float3 min_corner = {-1.0f, -1.0f, -1.0f};
    float voxel_size = 2.0f;

    while (node->offset != 0) {
        float half = voxel_size * 0.5f;
        unsigned child_index = 0;
        if (p.x >= min_corner.x + half) child_index |= 1, min_corner.x += half;
        if (p.y >= min_corner.y + half) child_index |= 2, min_corner.y += half;
        if (p.z >= min_corner.z + half) child_index |= 4, min_corner.z += half;
        voxel_size = half;
        node = &scene.nodes [node->offset + child_index];
    }

    LiteMath::float3 local = (p - min_corner) / voxel_size;
    auto lerp = [] (float a, float b, float t) { return a + t * (b - a); };

    float c00 = lerp (node->values [0], node->values [1], local.x);
    float c01 = lerp (node->values [4], node->values [5], local.x);
    float c10 = lerp (node->values [3], node->values [2], local.x);
    float c11 = lerp (node->values [7], node->values [6], local.x);

    float c0 = lerp (c00, c10, local.y);
    float c1 = lerp (c01, c11, local.y);

    return lerp (c0, c1, local.z);
}

}

