#pragma once

#include "data_channel_info.hpp"

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <limits>

/*
Data Channel is a simple structure for custom geometry attributes.
e.g. meshes can have vertex (normal, uv) or face (material id) attributes.
Each attribute is a k-component vector of some type (float3 for normal, int for material id).
There are only a few types here for simplicity, but more can be easily added (with changes in render).
DataChannel stores information about one attribute for all points/faces/primitives, so it encourages
usage of structure-of-arrays patters which generally a good idea for such task.
*/
struct DataChannel {
    static constexpr double LIMIT_UNDEFINED = 1e100;
    enum class Type {
        INT,
        FLOAT,
        FP8,
    };

    std::string name = "unnamed_channel";
    int num_components = 1;
    Type type = Type::FLOAT;
    double min_val = LIMIT_UNDEFINED;
    double max_val = LIMIT_UNDEFINED;
    std::vector <int> data_i;
    std::vector <float> data_f;
    std::vector <unsigned char> data_fp8;
};

void save_data_channel (std::ofstream &fs, const DataChannel &channel);
void load_data_channel (std::ifstream &fs, DataChannel &channel);
uint64_t calculate_bytesize (const DataChannel &channel);
DataChannelInfo get_info (const DataChannel &channel);

