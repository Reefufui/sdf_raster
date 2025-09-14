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

}

