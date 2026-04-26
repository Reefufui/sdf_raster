#include <string>
#include <vector>

#include "gui/GUIApplication.hpp"
#include "cli/CLIApplication.hpp"
#include "logger.hpp"

int main (int argc, char* argv[]) {
    sdf_raster::log_app_start ();
    int exit_code = EXIT_SUCCESS;

    try {
        if (argc > 1) {
            sdf_raster::CLIApplication cli (argc, argv);
            exit_code = cli.run ();
        } else {
            sdf_raster::GUIApplication app {};
            app.run ();
        }
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}
