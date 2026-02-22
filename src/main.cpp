#include <string>
#include <vector>

#include "application.hpp"
#include "logger.hpp"

int main (int argc, char* argv[]) {
    sdf_raster::log_app_start ();
    int exit_code = EXIT_SUCCESS;

    try {
        int width = 0;
        int height = 0;
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
            } else if (arg == "--single-frame") {
                single_frame = true;
            } else if (arg == "-render" && i + 1 < argc) {
                if (strcmp (argv [++i], "mesh") == 0) {
                    mesh_shader_support = true;
                } else if (strcmp (argv [++i], "compute") == 0) {
                } else {
                    throw std::runtime_error ("param usage of '-render': '-render <compute|mesh>'");
                }
            }
        }

        if (headless_mode) {
            sdf_raster::Application app (width, height);
            app.marching_cubes_cpu ("./assets/sdf/lowpoly_bunny.octree", filename);
        } else {
            sdf_raster::Application app (width, height, mesh_shader_support);
            app.run (single_frame);
        }
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}

