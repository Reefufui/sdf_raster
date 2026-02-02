#include <iostream>
#include <string>
#include <vector>

#include "application.hpp"

int main (int argc, char* argv[]) {
    try {
        int width = 800;
        int height = 600;
        size_t leaf_memory_limit = 208666624000; // 199mb
        std::string filename = "";
        bool headless_mode = false;
        bool single_frame = false;
        bool mesh_shader_support = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-out" && i + 1 < argc) {
                headless_mode = true;
                filename = argv[++i];
            } else if (arg == "-w" && i + 1 < argc) {
                width = std::stoi (argv[++i]);
            } else if (arg == "-h" && i + 1 < argc) {
                height = std::stoi (argv[++i]);
            } else if (arg == "--leaf-memory-limit" && i + 1 < argc) {
                leaf_memory_limit = std::stoi (argv[++i]);
            } else if (arg == "--single-frame") {
                single_frame = true;
            } else if (arg == "-render" && i + 1 < argc) {
                if (argv [++i] == "mesh") {
                    mesh_shader_support = true;
                } else if (argv [++i] == "compute") {
                } else {
                    throw std::runtime_error ("param usage of '-render': '-render <compute|mesh>'");
                }
            }
        }

        if (headless_mode) {
            sdf_raster::Application app (width, height);
            app.marching_cubes_cpu ("./assets/sdf/lowpoly_bunny.octree", filename);
        } else {
            sdf_raster::Application app (width, height, "sdf_raster", leaf_memory_limit, mesh_shader_support);
            app.run (single_frame);
        }
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

