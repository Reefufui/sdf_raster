#include <iostream>
#include <string>
#include <vector>

#include "application.hpp"

int main (int argc, char* argv[]) {
    try {
        int width = 800;
        int height = 600;
        std::string filename = "";
        bool headless_mode = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-out" && i + 1 < argc) {
                headless_mode = true;
                filename = argv[++i];
            } else if (arg == "-w" && i + 1 < argc) {
                width = std::stoi(argv[++i]);
            } else if (arg == "-h" && i + 1 < argc) {
                height = std::stoi(argv[++i]);
            }
        }

        if (headless_mode) {
            sdf_raster::Application app (width, height);
            app.marching_cubes_cpu ("./assets/sdf/example_octree_large.octree", filename);
        } else {
            sdf_raster::Application app (width, height, "sdf_raster");
            app.run ();
        }
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

