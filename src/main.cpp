// main.cpp
#include <iostream>
#include <string>
#include <vector>

#include "application/cli/cli_application.hpp"
#include "application/gui/gui_application.hpp"
#include "logger.hpp"
#include "state.hpp"

namespace sdf_raster {

struct AppConfig {
    std::string config_path = "/tmp/sdf_raster.json";
    bool headless = false;
};

AppConfig parse_args (int argc, char* argv []) {
    AppConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv [i];
        if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--session" && i + 1 < argc) {
            config.config_path = argv [++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv [0] << " [options]\n"
                      << "  --headless        Run in CLI mode\n"
                      << "  --session <path>  Session file (default: /tmp/sdf_raster.json)\n"
                      << "  --help            Show this help\n";
            std::exit (EXIT_SUCCESS);
        }
    }
    return config;
}

} // namespace sdf_raster

int main (int argc, char* argv []) {
    sdf_raster::log_app_start ();

    sdf_raster::AppConfig config = sdf_raster::parse_args (argc, argv);

    sdf_raster::SessionState session;
    sdf_raster::load_session (session, config.config_path);

    int exit_code = EXIT_SUCCESS;
    try {
        if (config.headless) {
            sdf_raster::CLIApplication app (session, argc, argv);
            exit_code = app.run ();
        } else {
            sdf_raster::GUIApplication app (session);
            app.run ();
        }
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    sdf_raster::dump_session (session, config.config_path);
    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}