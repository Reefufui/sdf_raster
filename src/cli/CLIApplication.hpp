#pragma once

#include <cstddef>

namespace sdf_raster {

class CLIApplication {
public:
    explicit CLIApplication (int argc, char* argv[]);
    int run ();

private:
    int argc;
    char** argv;
};

}
