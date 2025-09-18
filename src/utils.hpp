#pragma once

#include <iostream>
#include <stdexcept>

#define MY_VK_CHECK(expr) \
{ \
    VkResult err = expr; \
    if (err != VK_SUCCESS) { \
        std::cerr << "Vulkan Error: " << err << " (" << #expr << ") at " \
        << __FILE__ << ":" << __LINE__ << std::endl; \
        throw std::runtime_error ("Vulkan operation failed: " + std::string (#expr)); \
    } \
}
