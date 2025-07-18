#include <iostream>

#include "application.hpp"

int main() {
    try {
        Application app(800, 600, "CPU Triangle Rasterizer (Vulkan Display)");
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

