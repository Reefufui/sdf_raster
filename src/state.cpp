// state.cpp

#include <fstream>

#include "logger.hpp"
#include "state_json.hpp"
#include "state.hpp"

namespace sdf_raster {

void dump_session (const SessionState& session, const std::string& filename) {
    try {
        json j = session;
        std::ofstream file (filename);
        if (file.is_open ()) {
            file << j.dump (4);
        }
    } catch (const json::exception& e) {
        LOG_ERROR ("JSON session dump error: {}", e.what ());
    }
}

void load_session (SessionState& session, const std::string& filename) {
    try {
        std::ifstream file (filename);
        if (!file.is_open ()) {
            LOG_WARN("Session file not found {}. Using default session.", filename);
            return;
        }
        json j = json::parse (file);
        j.get_to (session);
    } catch (const json::exception& e) {
        LOG_ERROR ("JSON session load error: {}", e.what ());
    }
}

} // namespace sdf_raster

