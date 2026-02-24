#include <string>
#include <vector>

#include "application.hpp"
#include "logger.hpp"

int main (int argc, char* argv[]) {
    sdf_raster::log_app_start ();
    int exit_code = EXIT_SUCCESS;

    try {
        std::string filename = "";
        bool headless_mode = false;
        bool single_frame = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-out" && i + 1 < argc) {
                headless_mode = true;
                filename = argv[++i];
            } else if (arg == "--single-frame") {
                single_frame = true;
            }
        }

        if (headless_mode) {
            // sdf_raster::Application app (width, height);
            // app.marching_cubes_cpu ("./assets/sdf/lowpoly_bunny.octree", filename);
        } else {
            sdf_raster::Application app {};
            app.run (single_frame);
        }
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}

