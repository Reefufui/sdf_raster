// application/cli/cli_application.cpp
#include "cli_application.hpp"

#include "logger.hpp"

namespace sdf_raster {

CLIApplication::CLIApplication (int argc, char* argv[])
    : argc (argc)
    , argv (argv) {
}

int CLIApplication::run () {
    LOG_INFO ("Hello World");
    return EXIT_SUCCESS;
}

}
