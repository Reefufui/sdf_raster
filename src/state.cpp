// state.cpp

#include <fstream>

#include "logger.hpp"
#include "state_json.hpp"
#include "state.hpp"

namespace sdf_raster {

void dump_state(const Settings& settings, const std::string& filename) {
    try {
        json j = settings;
        std::ofstream file (filename);
        file << j.dump (4);
    } catch (const json::exception& e) {
        LOG_ERROR ("JSON dump error: {}", e.what ());
    }
}

void load_state (Settings& settings, const std::string& filename) {
    try {
        std::ifstream file (filename);
        if (!file.is_open ()) {
            LOG_WARN ("Settings file not found {}. Using default settings.", filename);
            return;
        }

        json j = json::parse (file);
        j.get_to (settings);
    } catch (const json::exception& e) {
        LOG_ERROR ("JSON load error: {}", e.what ());
    }
}

} // namespace sdf_raster

