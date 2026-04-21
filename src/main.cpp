#include <string>
#include <vector>

#include "application.hpp"
#include "logger.hpp"

int main (int argc, char* argv[]) {
    sdf_raster::log_app_start ();
    int exit_code = EXIT_SUCCESS;

    if (argc > 1) {
        LOG_ERROR ("Unexpected argument: {}.", argv [1]);
        exit_code = EXIT_FAILURE;
        sdf_raster::log_app_exit (exit_code);
        return exit_code;
    }

    try {
        sdf_raster::Application app {};
        app.run ();
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}

