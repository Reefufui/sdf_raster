#pragma once

#include "./data_channel/data_channel.hpp"
#include "./header.hpp"

#include <cstdint>
#include <vector>

namespace sdf_raster {
namespace scom2 {

struct SCom2Tree {
    std::string name;

    scom2::Header header;
    std::vector <uint32_t> nodes;
    std::vector <uint32_t> bricks;

    std::vector <DataChannel> point_channels;
    std::vector <DataChannel> voxel_channels;
};

} // scom2
} // sdf_raster

