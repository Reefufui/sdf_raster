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

    int cli_argc = argc;
    std::vector <char*> cli_argv (argv, argv + argc);

    sdf_raster::AppConfig config;
    int i = 1;
    while (i < cli_argc) {
        std::string arg = cli_argv [i];
        if (arg == "--headless") {
            config.headless = true;
            cli_argv.erase (cli_argv.begin () + i);
            cli_argc--;
        } else if (arg == "--session" && i + 1 < cli_argc) {
            config.config_path = cli_argv [i + 1];
            cli_argv.erase (cli_argv.begin () + i, cli_argv.begin () + i + 2);
            cli_argc -= 2;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv [0] << " [options]\n"
                      << "  --headless        Run in CLI mode\n"
                      << "  --session <path>  Session file (default: /tmp/sdf_raster.json)\n"
                      << "  --help            Show this help\n";
            std::exit (EXIT_SUCCESS);
        } else {
            ++i;
        }
    }

    sdf_raster::SessionState session;
    sdf_raster::load_session (session, config.config_path);

    int exit_code = EXIT_SUCCESS;
    try {
        if (config.headless) {
            sdf_raster::CLIApplication app (session, cli_argc, cli_argv.data ());
            exit_code = app.run ();
        } else {
            sdf_raster::GUIApplication app (session);
            app.run ();
        }
    } catch (const std::exception& e) {
        LOG_CRITICAL ("exception: {}", e.what ());
        exit_code = EXIT_FAILURE;
    }

    if (!config.headless) {
        sdf_raster::dump_session (session, config.config_path);
    }
    sdf_raster::log_app_exit (exit_code);

    return exit_code;
}
