// main.cpp
#include <filesystem>
#include <iostream>
#include <optional>
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
    std::optional<std::filesystem::path> export_mesh_path;
};

AppConfig parse_args (int argc, char* argv []) {
    AppConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv [i];
        if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--session" && i + 1 < argc) {
            config.config_path = argv [++i];
        } else if (arg == "--export-mesh" && i + 1 < argc) {
            config.export_mesh_path = std::filesystem::path (argv [++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv [0] << " [options]\n"
                      << "  --headless              Run in CLI mode\n"
                      << "  --session <path>        Session file (default: /tmp/sdf_raster.json)\n"
                      << "  --export-mesh <path>    Convert scene to OBJ at <path> (headless only)\n"
                      << "  --help                  Show this help\n";
            std::exit (EXIT_SUCCESS);
        }
    }
    return config;
}

} // namespace sdf_raster

int main (int argc, char* argv []) {
    sdf_raster::log_app_start ();

    sdf_raster::AppConfig config;
    std::vector<std::string> remaining_args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv [i];
        if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--session" && i + 1 < argc) {
            config.config_path = argv [++i];
        } else if (arg == "--export-mesh") {
            if (i + 1 < argc) {
                config.export_mesh_path = std::filesystem::path (argv [i + 1]);
            }
            remaining_args.push_back (arg);
            if (i + 1 < argc) {
                remaining_args.push_back (argv [++i]);
            }
        } else if (arg == "--export-mesh-cpu-depth") {
            remaining_args.push_back (arg);
            if (i + 1 < argc) {
                remaining_args.push_back (argv [++i]);
            }
        } else if (arg == "--export-mesh-max-lod") {
            remaining_args.push_back (arg);
            if (i + 1 < argc) {
                remaining_args.push_back (argv [++i]);
            }
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv [0] << " [options]\n"
                      << "  --headless              Run in CLI mode\n"
                      << "  --session <path>        Session file (default: /tmp/sdf_raster.json)\n"
                      << "  --export-mesh <path>    Convert scene to OBJ at <path> (headless only)\n"
                      << "  --help                  Show this help\n";
            std::exit (EXIT_SUCCESS);
        } else {
            remaining_args.push_back (arg);
        }
    }

    int cli_argc = static_cast<int> (remaining_args.size ());
    std::vector<std::string> full_args;
    full_args.push_back ("sdf_raster");
    for (auto& s : remaining_args) {
        full_args.push_back (s);
    }
    std::vector<char*> cli_argv_ptrs;
    for (auto& s : full_args) {
        cli_argv_ptrs.push_back (s.data ());
    }
    cli_argc = static_cast<int> (full_args.size ());

    sdf_raster::SessionState session;
    sdf_raster::load_session (session, config.config_path);

    if (config.export_mesh_path && !config.headless) {
        LOG_WARN ("--export-mesh only works in headless mode; ignoring");
        config.export_mesh_path.reset ();
    }

    int exit_code = EXIT_SUCCESS;
    try {
        if (config.headless) {
            sdf_raster::CLIApplication app (session, cli_argc, cli_argv_ptrs.data ());
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
